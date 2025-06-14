
#include "EntryPoints/Calibrate.hpp"

#include <iostream> // for standard I/O
#include <string>   // for strings
#include <sstream>  // string to number conversion

#include <filesystem>
#include <thread>
#include <mutex>

#include <opencv2/core.hpp>		// Basic OpenCV structures (cv::Mat, Scalar)
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>  // OpenCV window I/O
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/cvconfig.h>

#include <Misc/math2d.hpp>
#include <Misc/math3d.hpp>
#include <Misc/path.hpp>

#include <Visualisation/ImguiWindow.hpp>
#include <Visualisation/openGL/Texture.hpp>

#include <thirdparty/serialib.h>
#include <Misc/GlobalConf.hpp>
#include <Cameras/Camera.hpp>
#include <Cameras/VideoCaptureCamera.hpp>
#include <Cameras/Calibfile.hpp>
#include <Cameras/ImageTypes.hpp>
#include <Misc/FrameCounter.hpp>
#include <Misc/ManualProfiler.hpp>


using namespace std;
using namespace cv;
namespace fs = std::filesystem;

const string TempImgPath = "TempCalib";
const string CalibWindowName = "Calibration";


void CreateKnownBoardPos(Size BoardSize, double squareEdgeLength, vector<Point3f>& corners)
{
	for (int i = 0; i < BoardSize.height; i++)
	{
		for (int j = 0; j < BoardSize.width; j++)
		{
			corners.push_back(Point3f(j * squareEdgeLength, i * squareEdgeLength, 0));
		}   
	}
}

struct CameraCalibrationSourceData
{
	struct CalibrationImageData
	{
		vector<Point2f> CheckerboardImageSpacePoints;
		vector<Point3f> CheckerboardWorldSpacePoints;
		string ImagePath = "none";
		bool Use = true;
	};
	vector<CalibrationImageData> Images;
	Size FrameSize;
};

struct CameraCalibrationOutputData
{
	Mat CameraMatrix; 
	Mat DistanceCoefficients;
	vector<bool> KeptImages;
	float ReprojectionError;
	float Score; //num images / reprojection error
};


CameraCalibrationOutputData ExploreCalibrationWith(const CameraCalibrationSourceData &SourceData)
{
	vector<vector<Point2f>> ImageSpacePoints;
	vector<vector<Point3f>> WorldSpacePoints;
	int numimages = 0;
	for (auto &&i : SourceData.Images)
	{
		if (i.Use)
		{
			ImageSpacePoints.push_back(i.CheckerboardImageSpacePoints);
			WorldSpacePoints.push_back(i.CheckerboardWorldSpacePoints);
			numimages++;
		}
	}
	CameraCalibrationOutputData OutputData;

	vector<Mat> rVectors, tVectors;
	OutputData.CameraMatrix = Mat::eye(3, 3, CV_64F);
	OutputData.DistanceCoefficients = Mat::zeros(Size(4,1), CV_64F);

	OutputData.CameraMatrix = initCameraMatrix2D(WorldSpacePoints, ImageSpacePoints, SourceData.FrameSize);

	calibrateCamera(WorldSpacePoints, ImageSpacePoints, SourceData.FrameSize, 
		OutputData.CameraMatrix, OutputData.DistanceCoefficients, rVectors, tVectors, 
		CALIB_ZERO_TANGENT_DIST, TermCriteria(TermCriteria::COUNT, 50, DBL_EPSILON));
	vector<float> reprojectionErrors;
	float reprojectionErrorTotal = 0;
	reprojectionErrors.resize(numimages);
	int imageidx = 0;
	for (auto &&i : SourceData.Images)
	{
		OutputData.KeptImages.push_back(i.Use);
		if (!i.Use)
		{
			continue;
		}
		vector<Point2f> reprojected;

		projectPoints(i.CheckerboardWorldSpacePoints, rVectors[imageidx], tVectors[imageidx], OutputData.CameraMatrix, OutputData.DistanceCoefficients, reprojected);
		reprojectionErrors[imageidx] = ComputeReprojectionError(i.CheckerboardImageSpacePoints, reprojected) / reprojected.size();
		reprojectionErrorTotal += reprojectionErrors[imageidx];
		imageidx++;
	}
	OutputData.ReprojectionError = reprojectionErrorTotal;
	OutputData.Score = pow(numimages, GetCalibrationConfig().NumImagePower) / (reprojectionErrorTotal+GetCalibrationConfig().ReprojectionErrorOffset);
	return OutputData;
}

CameraCalibrationOutputData CameraCalibration(CameraCalibrationSourceData &SourceData)
{
	CameraCalibrationOutputData BestOutputData;
	BestOutputData.Score = 0;

	int numimages = SourceData.Images.size();
	
	//disable all images
	for (auto &&image : SourceData.Images)
	{
		image.Use = true;
		BestOutputData.KeptImages.push_back(true);
	}
	uint64_t iteration = 0;
	BestOutputData = ExploreCalibrationWith(SourceData);
	while (1)
	{
		CameraCalibrationOutputData CurrentBest = BestOutputData;
		cout << "Iteration " << iteration << " | Score : " << BestOutputData.Score 
			<< " (Reprojection error is " << BestOutputData.ReprojectionError/BestOutputData.KeptImages.size() << "px/pt)" << endl;
		iteration++;
		for (int i = 0; i < numimages; i++)
		{
			SourceData.Images[i].Use = CurrentBest.KeptImages[i];
		}

		for (auto &&image : SourceData.Images)
		{
			if (!image.Use)
			{
				continue;
			}
			image.Use = false;
			auto OutputData = ExploreCalibrationWith(SourceData);
			if (OutputData.Score > CurrentBest.Score)
			{
				CurrentBest = OutputData;
			}
			image.Use = true;
		}

		if (CurrentBest.Score > BestOutputData.Score)
		{
			for (int i = 0; i < numimages; i++)
			{
				if (CurrentBest.KeptImages[i] != BestOutputData.KeptImages[i])
				{
					cout << "Iteration " << iteration << " ejected image " << SourceData.Images[i].ImagePath << endl;
				}
			}
			BestOutputData = CurrentBest;
		}
		else
		{
			break;
		}
		
	}

	cout << "Best calibration done with :" << endl;
	for (int i = 0; i < numimages; i++)
	{
		if (BestOutputData.KeptImages[i])
		{
			cout << "\t- " << SourceData.Images[i].ImagePath << endl;
		}
	}
	cout << "Score : " << BestOutputData.Score 
		<< " (Reprojection error is " << BestOutputData.ReprojectionError/BestOutputData.KeptImages.size() << "px/pt)" << endl;

	return BestOutputData;
}

vector<String> GetPathsToCalibrationImages()
{
	vector<String> pathos;
	for (const auto & entry : fs::directory_iterator(TempImgPath))
	{
		pathos.push_back(entry.path().string());
		//cout << entry.path().string() << endl;
	}
	return pathos;
}

int GetCalibrationImagesLastIndex(vector<String> Pathes)
{
	int next = -1;
	for (size_t i = 0; i < Pathes.size(); i++)
	{
		String stripped = Pathes[i].substr(TempImgPath.length()+1, Pathes[i].length() - TempImgPath.length()-1 - String(".png").length());
		try
		{
			int thatidx = 0;
			sscanf(Pathes[i].c_str(), "%d", &thatidx);
			next = next < thatidx ? thatidx : next;
		}
		catch(const std::exception& e)
		{
			cout << "Failed to stoi " << stripped <<endl;
			//std::cerr << e.what() << '\n';
		}
	}
	return next;
}

Size ReadAndCalibrateLens(LensSettings &Lens)
{
	auto calconf = GetCalibrationConfig();
	Size CheckerSize = calconf.NumIntersections;
	vector<String> pathes = GetPathsToCalibrationImages();
	size_t numpathes = pathes.size();

	CameraCalibrationSourceData SourceData;
	SourceData.Images.resize(numpathes);

	vector<Size> resolutions;
	resolutions.resize(numpathes);

	bool hasROI = Lens.ROI.area() > 0;

	parallel_for_(Range(0, numpathes), [&](const Range InRange)
	{
	//Range InRange(0, numpathes);
		for (int i = InRange.start; i < InRange.end; i++)
		{
			auto &ThisImageData = SourceData.Images[i];
			ThisImageData.ImagePath = pathes[i];
			Mat frame = imread(ThisImageData.ImagePath, IMREAD_GRAYSCALE);
			resolutions[i] = frame.size();
			vector<Point2f> foundPoints;
			Mat frame_cropped = hasROI ? frame(Lens.ROI) : frame;
			bool found = findChessboardCorners(frame_cropped, CheckerSize, foundPoints, CALIB_CB_ADAPTIVE_THRESH | CALIB_CB_NORMALIZE_IMAGE);
			if (found)
			{
				TermCriteria criteria(TermCriteria::COUNT | TermCriteria::EPS, 100, 0.001);
				cornerSubPix(frame_cropped, foundPoints, Size(4,4), Size(-1,-1), criteria);
				//Scalar sharpness = estimateChessboardSharpness(frame, CheckerSize, foundPoints);
				ThisImageData.CheckerboardImageSpacePoints = foundPoints;
				CreateKnownBoardPos(CheckerSize, calconf.SquareSideLength/1000.f, ThisImageData.CheckerboardWorldSpacePoints);
				ThisImageData.Use = true;
			}
			else
			{
				ThisImageData.Use = false;
				cout << "Failed to find chessboard in image " << ThisImageData.ImagePath << " index " << i << endl;
			}
			
		}
	});

	cout << "Images are done loading, starting calibration..." <<endl;
	for (int i = SourceData.Images.size() - 1; i >= 0; i--)
	{
		auto &ThisImageData = SourceData.Images[i];
		if (!ThisImageData.Use)
		{
			resolutions.erase(resolutions.begin() + i);
			SourceData.Images.erase(SourceData.Images.begin() + i);
		}
	}
	
	vector<Size> sizes;
	for (size_t i = 0; i < SourceData.Images.size(); i++)
	{
		bool hasres = false;
		for (size_t j = 0; j < sizes.size(); j++)
		{
			if (sizes[j] == resolutions[i])
			{
				hasres = true;
				break;
			}
		}
		if (!hasres)
		{
			sizes.push_back(resolutions[i]);
		}
	}
	
	if (sizes.size() == 1)
	{
		SourceData.FrameSize = sizes[0];
		auto OutputData = CameraCalibration(SourceData);
		Lens.CameraMatrix = OutputData.CameraMatrix;
		Lens.distanceCoeffs = OutputData.DistanceCoefficients;
		cout << "Calibration done ! Matrix : " << Lens.CameraMatrix << " / Distance Coefficients : " << Lens.distanceCoeffs << endl;
		return sizes[0];
	}
	else if (sizes.size() == 0)
	{
		cout << "Il faut prendre des images pour pouvoir calibrer la caméra quand même..." << endl;
	}
	else
	{
		cerr << "ERROR : " << sizes.size() << " different resolutions were used in the calibration. That's fixable but fuck you." << endl;
		for (size_t i = 0; i < sizes.size(); i++)
		{
			cerr << "@" << sizes[i] <<endl;
			for (size_t j = 0; j < numpathes; j++)
			{
				cerr << " -" << pathes[j] <<endl;
			}
		}
	}
	return Size(0,0);
}

void ReadAndCalibrateStereo(std::vector<LensSettings> &Lenses)
{
	auto calconf = GetCalibrationConfig();
	Size CheckerSize = calconf.NumIntersections;
	vector<String> pathes = GetPathsToCalibrationImages();
	size_t numpathes = pathes.size();
	size_t numlenses = Lenses.size();
	assert(numlenses > 0);

	vector<CameraCalibrationSourceData> SourceData;
	SourceData.resize(numlenses);
	for (size_t i = 0; i < numlenses; i++)
	{
		SourceData[i].Images.resize(numpathes);
	}
	
	

	parallel_for_(Range(0, numpathes*numlenses), [&](const Range InRange)
	{
	//Range InRange(0, numpathes);
		for (int i = InRange.start; i < InRange.end; i++)
		{
			int lensidx = i%numlenses;
			int imageidx = i/numlenses;
			auto &ThisImageData = SourceData[lensidx].Images[imageidx];
			ThisImageData.ImagePath = pathes[imageidx];
			Mat frame = imread(ThisImageData.ImagePath, IMREAD_GRAYSCALE);
			vector<Point2f> foundPoints;
			Mat frame_cropped = frame(Lenses[lensidx].ROI);
			bool found = findChessboardCorners(frame_cropped, CheckerSize, foundPoints, CALIB_CB_ADAPTIVE_THRESH | CALIB_CB_NORMALIZE_IMAGE);
			if (found)
			{
				TermCriteria criteria(TermCriteria::COUNT | TermCriteria::EPS, 100, 0.001);
				cornerSubPix(frame_cropped, foundPoints, Size(4,4), Size(-1,-1), criteria);
				//Scalar sharpness = estimateChessboardSharpness(frame, CheckerSize, foundPoints);
				ThisImageData.CheckerboardImageSpacePoints = foundPoints;
				CreateKnownBoardPos(CheckerSize, calconf.SquareSideLength/1000.f, ThisImageData.CheckerboardWorldSpacePoints);
				ThisImageData.Use = true;
			}
			else
			{
				ThisImageData.Use = false;
				cout << "Failed to find chessboard in image " << ThisImageData.ImagePath << " index " << i << endl;
			}
			
		}
	});

	vector<vector<vector<Point2f>>> ImageSpacePoints;
	ImageSpacePoints.resize(numlenses);
	vector<vector<Point3f>> WorldSpacePoints;

	for (size_t imageidx = 0; imageidx < numpathes; imageidx++)
	{
		bool keep = true;
		for (size_t lensidx = 0; lensidx < numlenses; lensidx++)
		{
			if (SourceData[lensidx].Images[imageidx].Use == false)
			{
				keep = false;
				break;
			}
			
		}
		if (!keep)
		{
			cout << "Could not keep image " << SourceData[0].Images[imageidx].ImagePath << " for stereo calibration" << endl;
			continue;
		}
		WorldSpacePoints.push_back(SourceData[0].Images[imageidx].CheckerboardWorldSpacePoints);

		for (size_t lensidx = 0; lensidx < numlenses; lensidx++)
		{
			ImageSpacePoints[lensidx].push_back(SourceData[lensidx].Images[imageidx].CheckerboardImageSpacePoints);
		}
	}
	
	assert(numlenses == 2);
	Mat R, T, E, F;
	double reprojectionError = stereoCalibrate(WorldSpacePoints, ImageSpacePoints[0], ImageSpacePoints[1], Lenses[0].CameraMatrix, Lenses[0].distanceCoeffs, Lenses[1].CameraMatrix, Lenses[1].distanceCoeffs, Lenses[0].ROI.size(), R, T, E, F, CALIB_FIX_INTRINSIC);
	cout << "Stereo calibration done with " << WorldSpacePoints.size() << " images, R=" << R << " T=" << T << " E=" << E << " F=" << F << endl;
	
	Lenses[1].LensPosition = Affine3d(R,T);
}

Camera* CamToCalib;
std::vector<Rect> ROIs;

bool Calibrating = false;
bool ShowUndistorted = false;

void CalibrationWorker()
{
	Calibrating = true;

	std::vector<LensSettings> lenses;
	for (size_t i = 0; i < ROIs.size() || i == 0; i++)
	{
		LensSettings lens;
		if (ROIs.size() > i)
		{
			lens.ROI = ROIs[i];
		}
		
		Size resolution = ReadAndCalibrateLens(lens);
		if (CamToCalib->connected)
		{
			//CamToCalib->SetCalibrationSetting(lens.CameraMatrix, lens.distanceCoeffs);
			if (resolution != CamToCalib->GetCameraSettings()->Resolution)
			{
				cerr << "WARNING : Resolution of the stored images isn't the same as the resolution of the live camera!" <<endl;
			}
		}
		else
		{
			VideoCaptureCameraSettings CamSett = *dynamic_cast<const VideoCaptureCameraSettings*>(CamToCalib->GetCameraSettings());

			CamSett.Resolution = resolution;
			CamSett.Lenses.resize(1);
			CamSett.Lenses[0].CameraMatrix = lens.CameraMatrix;
			CamSett.Lenses[0].distanceCoeffs = lens.distanceCoeffs;
			CamSett.DeviceInfo.device_description = "NoCam";
			CamToCalib->SetCameraSetting(make_shared<VideoCaptureCameraSettings>(CamSett));
		}
		
		
		auto calconf = GetCalibrationConfig();
		double apertureWidth = calconf.SensorSize.width, apertureHeight = calconf.SensorSize.height, fovx, fovy, focalLength, aspectRatio;
		Point2d principalPoint;
		calibrationMatrixValues(lens.CameraMatrix, resolution, apertureWidth, apertureHeight, fovx, fovy, focalLength, principalPoint, aspectRatio);
		cout << "Computed camera parameters for sensor of size " << apertureWidth << "x" << apertureHeight <<"mm :" << endl
		<< " fov:" << fovx << "x" << fovy << "°, focal length=" << focalLength << ", aspect ratio=" << aspectRatio << endl
		<< "Principal point @ " << principalPoint << endl;

		lenses.push_back(lens);

	}
	if (lenses.size() == 2)
	{
		ReadAndCalibrateStereo(lenses);
	}
	CamToCalib->SetLensSetting(lenses);
	
	const CameraSettings* CamSett = CamToCalib->GetCameraSettings();
	
	string filename = "noname";
	const VideoCaptureCameraSettings* VCCamSett = dynamic_cast<const VideoCaptureCameraSettings*>(CamSett);
	if (VCCamSett)
	{
		filename = VCCamSett->DeviceInfo.device_description;
	}


	

	writeCameraParameters(GetCyclopsPath() / "build" / filename, *CamSett);
	//distanceCoefficients = Mat::zeros(8, 1, CV_64F);
	ShowUndistorted = true;
	Calibrating = false;
}

void UpdateROIs(bool Stereo, Size resolution)
{
	ROIs.resize(1+Stereo);
	if (Stereo)
	{
		ROIs[0] = Rect(0,0,resolution.width/2, resolution.height);
		ROIs[1] = Rect(resolution.width/2,0,resolution.width/2, resolution.height);
	}
	else
	{
		ROIs[0] = Rect(cv::Point2i(), resolution);
	}
}

bool docalibration(VideoCaptureCameraSettings CamSett)
{
	unique_ptr<thread> CalibrationThread;
	ManualProfiler<false> prof;
	bool HasCamera = CamSett.IsValid();
	
	if (HasCamera)
	{
		CamToCalib = new VideoCaptureCamera(make_shared<VideoCaptureCameraSettings>(CamSett));
		CamToCalib->StartFeed();
	}
	else
	{
		CamToCalib = nullptr;
	}
	

	bool AutoCapture = false;
	float AutoCaptureFramerate = 2;
	double AutoCaptureStart;
	int LastAutoCapture;
	bool Stereo = false;

	
	fs::create_directory(TempImgPath);

	//namedWindow(CalibWindowName, WINDOW_NORMAL);
	//setWindowProperty(CalibWindowName, WND_PROP_FULLSCREEN, WINDOW_FULLSCREEN);

	if (!HasCamera)
	{
		cout << "No camera was found, calibrating from saved images" << endl;
		CalibrationWorker();
		vector<String> pathes = GetPathsToCalibrationImages();
		for (size_t i = 0; i < pathes.size(); i++)
		{
			Mat image = imread(pathes[i]);
			UMat image2, undist;
			image.copyTo(image2);
			CamToCalib->Read();
			CamToCalib->Undistort();
			CameraImageData Frame = CamToCalib->GetFrame(false);
			imshow(CalibWindowName, undist);
			waitKey(1000);
		}
		
		return true;
	}
	CamToCalib->StartFeed();
	UpdateROIs(Stereo, CamSett.Resolution);
	ImguiWindow imguiinst;
	imguiinst.Init();


	cout << "Camera calibration mode !" << endl
	<< "Press [space] to capture an image, [enter] to calibrate, [a] to capture an image every " << 1/AutoCaptureFramerate << "s" <<endl
	<< "Take pictures of a checkerboard with " << GetCalibrationConfig().NumIntersections.width+1 << "x" << GetCalibrationConfig().NumIntersections.height+1 << " squares of side length " << GetCalibrationConfig().SquareSideLength << "mm" << endl
	<< "Images will be saved in folder " << TempImgPath << endl
	<< "Camera opened with resolution " << CamToCalib->GetCameraSettings()->Resolution << endl;

	
	//startWindowThread();
	
	vector<String> pathes = GetPathsToCalibrationImages();
	int nextIdx = GetCalibrationImagesLastIndex(pathes) +1;
	int64 lastCapture = getTickCount();

	FrameCounter fps;
	int failed = 0;
	bool mirroredX = false, mirroredY = false;
	while (true)
	{
		prof.EnterSection("StartFrame");
		imguiinst.StartFrame();

		UMat frame;
		bool CaptureImageThisFrame = false;
		prof.EnterSection("Read frame");
		if (!CamToCalib->Read())
		{
			//cout<< "read fail" <<endl;
			imguiinst.EndFrame();
			failed++;
			if (failed >10)
			{
				break;
			}
			continue;
		}
		prof.EnterSection("Controls");
		if (ImGui::Begin("Controls"))
		{
			ImGui::Text("FPS : %f", 1/fps.GetDeltaTime());
			ImGui::Checkbox("Mirror X", &mirroredX);
			ImGui::Checkbox("Mirror Y", &mirroredY);
		}
		if (ShowUndistorted)
		{
			prof.EnterSection("Undistort");
			CamToCalib->Undistort();
			prof.EnterSection("Controls");
		}
		CameraImageData framedata = CamToCalib->GetFrame(!ShowUndistorted);
		frame = framedata.Image;

		if (!ShowUndistorted)
		{
			auto stereo_before = Stereo;
			ImGui::Checkbox("Stereo Camera", &Stereo);
			if (Stereo != stereo_before)
			{
				UpdateROIs(Stereo, CamSett.Resolution);
			}
			
			if(ImGui::Checkbox("Auto capture", &AutoCapture))
			{
				//AutoCapture = !AutoCapture;
				AutoCaptureStart = fps.GetAbsoluteTime();
				LastAutoCapture = 0;
			}
			ImGui::SliderFloat("Auto capture framerate", &AutoCaptureFramerate, 1, 10);
		}
		if(ImGui::Button("Capture Image"))
		{
			//save image
			if (!ShowUndistorted)
			{
				CaptureImageThisFrame = true;
			}
		}
		if(ImGui::Button("Calibrate"))
		{
			if (Calibrating)
			{
				AutoCapture = false;
			}
			else
			{
				if (ShowUndistorted)
				{
					ShowUndistorted = false;
				}
				else
				{
					CalibrationThread = make_unique<thread>(CalibrationWorker);
				}
			}
		}

		if (AutoCapture)
		{
			int autoCaptureIdx = floor((fps.GetAbsoluteTime() - AutoCaptureStart)*AutoCaptureFramerate);
			if (autoCaptureIdx > LastAutoCapture)
			{
				LastAutoCapture++;
				CaptureImageThisFrame = true;
			}
			
		}
		if (CaptureImageThisFrame && !Calibrating && !ShowUndistorted)
		{
			//vector<Point2f> foundPoints;
			//UMat grayscale;
			//cvtColor(frame, grayscale, COLOR_BGR2GRAY);
			//bool found = checkChessboard(grayscale, CheckerSize);
			prof.EnterSection("Save");
			imwrite(TempImgPath + "/" + to_string(nextIdx++) + ".png", frame);
			lastCapture = getTickCount() + getTickFrequency();
			prof.EnterSection("Controls");
		}
		
		if (Calibrating)
		{
			ImGui::Text("Calibrating, please wait...");
		}
		else if (getTickCount() < lastCapture)
		{
			ImGui::Text("Image %d %s !", nextIdx -1, ( AutoCapture ? "AutoCaptured" : "captured"));
		}

		prof.EnterSection("Background");


		{
			cv::Rect Backgroundsize(Point2i(0,0), (Size2i)imguiinst.GetWindowSize());
			cv::Rect impos = ScaleToFit(frame.size(), Backgroundsize);
			Size2f uv_min(0,0), uv_max(1,1);

			if (mirroredX)
			{
				float temp = uv_min.width;
				uv_min.width = uv_max.width;
				uv_max.width = temp;
			}
			if (mirroredY)
			{
				float temp = uv_min.height;
				uv_min.height = uv_max.height;
				uv_max.height = temp;
			}
			
			imguiinst.AddImageToBackground(0, frame, impos, uv_min, uv_max);
		}

		prof.EnterSection("End Frame");
		ImGui::End();
		if(!imguiinst.EndFrame())
		{
			
			break;
		}
		prof.EnterSection("");
		prof.PrintIfShould();
		//fps.AddFpsToImage(frameresized, fps.GetDeltaTime());
		//imshow(CalibWindowName, frameresized);
	}
	if (CalibrationThread)
	{
		CalibrationThread->join();
		CalibrationThread.reset();
	}
	return true;
}
