#include "Misc/GlobalConf.hpp"

#include <Misc/ArucoDictSize.hpp>
#include <Cameras/ImageTypes.hpp>

#include <iostream>
#include <libconfig.h++>
#include <XScreenSize.hpp>

using namespace std;
using namespace cv;
using namespace libconfig;

int ProgramRunType = (int)RunType::Normal;
aruco::ArucoDetector ArucoDet;
bool HasDetector = false;
vector<UMat> MarkerImages;

bool ConfigInitialised = false;
Config cfg;

//Default values
CaptureConfig CaptureCfg = {(int)CameraStartType::ANY, Size(1920,1080), 1.f, 30, 1, ""};
vector<InternalCameraConfig> CamerasInternal;
CalibrationConfig CamCalConf = {40, Size(6,4), 0.5, 1.5, Size2d(4.96, 3.72)};

bool HasScreenData = false;
Size screenresolution(-1,-1);
Size2d screensize(-1,-1);

template<class T>
Setting& EnsureExistCfg(Setting& Location, const char *FieldName, Setting::Type SettingType, T DefaultValue)
{
	if (!Location.exists(FieldName))
	{
		Setting& settingloc = Location.add(FieldName, SettingType);
		switch (SettingType)
		{
		case Setting::TypeGroup:
		case Setting::TypeArray:
		case Setting::TypeList:
			/* code */
			break;
		
		default:
			settingloc = DefaultValue;
			break;
		}
		return settingloc;
	}
	return Location[FieldName];
}

template<class T>
Setting& CopyDefaultCfg(Setting& Location, const char *FieldName, Setting::Type SettingType, T& DefaultValue)
{
	if (!Location.exists(FieldName))
	{
		Setting& settingloc = Location.add(FieldName, SettingType);
		switch (SettingType)
		{
		case Setting::TypeGroup:
		case Setting::TypeArray:
		case Setting::TypeList:
			/* code */
			break;
		
		default:
			settingloc = DefaultValue;
			break;
		}
		return settingloc;
	}
	else
	{
		DefaultValue = (T)Location[FieldName];
		return Location[FieldName];
	}
}

template<class T>
Setting& CopyDefaultVector(Setting& Location, const char *FieldName, Setting::Type SettingType, vector<T>& DefaultValue)
{
	if(Location.exists(FieldName))
	{
		if (Location[FieldName].isArray())
		{
			Setting& Arrayloc = Location[FieldName];
			DefaultValue.clear();
			for (int i = 0; i < Arrayloc.getLength(); i++)
			{
				DefaultValue.push_back(Arrayloc[i]);
			}
			return Arrayloc;
		}
		else
		{
			Location.remove(FieldName);
		}
		
	}
	Setting& settingloc = Location.add(FieldName, Setting::TypeArray);
	switch (SettingType)
	{
	case Setting::TypeGroup:
	case Setting::TypeArray:
	case Setting::TypeList:
		/* code */
		break;
	
	default:
		for (int i = 0; i < DefaultValue.size(); i++)
		{
			settingloc.add(SettingType) = DefaultValue[i];
		}
		
		break;
	}
	return settingloc;
}

void InitConfig()
{
	if (ConfigInitialised)
	{
		return;
	}
	
	bool err = false;
	try
	{
		cfg.readFile("../config.cfg");
	}
	catch(const FileIOException &fioex)
	{
		std::cerr << "I/O error while reading file." << std::endl;
		err = true;
	}
	catch(const ParseException &pex)
	{
		std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine()
			<< " - " << pex.getError() << std::endl;
		err = true;
	}

	Setting& root = cfg.getRoot();

	CopyDefaultCfg(root, "RunType", Setting::TypeInt, ProgramRunType);

	Setting& Capture = EnsureExistCfg(root, "Capture", Setting::Type::TypeGroup, 0);
	
	{
		Setting& Resolution = EnsureExistCfg(Capture, "Resolution", Setting::TypeGroup, 0);
		CopyDefaultCfg(Resolution, "Width", Setting::TypeInt, CaptureCfg.FrameSize.width);
		CopyDefaultCfg(Resolution, "Height", Setting::TypeInt, CaptureCfg.FrameSize.height);
		CopyDefaultCfg(Capture, "Framerate", Setting::TypeInt, CaptureCfg.CaptureFramerate);
		CopyDefaultCfg(Capture, "FramerateDivider", Setting::TypeInt, CaptureCfg.FramerateDivider);
		CopyDefaultCfg(Capture, "Method", Setting::TypeInt, CaptureCfg.StartType);
		CopyDefaultCfg(Resolution, "Reduction", Setting::TypeFloat, CaptureCfg.ReductionFactor);
		CopyDefaultCfg(Capture, "CameraFilter", Setting::TypeString, CaptureCfg.filter);
		
	}

	Setting& CamerasSett = EnsureExistCfg(root, "InternalCameras", Setting::Type::TypeList, 0);
	{
		CamerasInternal.clear();
		for (int i = 0; i < CamerasSett.getLength(); i++)
		{
			CamerasInternal.push_back(InternalCameraConfig());
			CamerasInternal[i].CameraName = "GarbageFilter";
			CamerasInternal[i].LocationRelative = Affine3d::Identity().translate(Vec3d(0.1,0.2,0.3));
			CopyDefaultCfg(CamerasSett[i], "Filter", Setting::TypeString, CamerasInternal[i].CameraName);
			Setting& Loc = EnsureExistCfg(CamerasSett[i], "Location", Setting::Type::TypeList, 0);

			for (int j = 0; j < 4; j++)
			{
				if (Loc.getLength() <= j)
				{
					Loc.add(Setting::TypeArray);
				}
				while (!Loc[j].isArray())
				{
					Loc.remove(j);
					Loc.add(Setting::TypeArray);
				}
				
				for (int k = 0; k < 4; k++)
				{
					double &address = CamerasInternal[i].LocationRelative.matrix(j,k);
					float value = address;
					if (Loc[j].getLength() <= k)
					{
						Loc[j].add(Setting::TypeFloat) = address;
					}
					else
					{
						address = Loc[j][k];
					}
				}
			}
		}
	}

	Setting& CalibSett = EnsureExistCfg(root, "Calibration", Setting::Type::TypeGroup, 0);
	{
		CopyDefaultCfg(CalibSett, "EdgeSize", Setting::TypeFloat, CamCalConf.SquareSideLength);
		CopyDefaultCfg(CalibSett, "NumIntersectionsX", Setting::TypeInt, CamCalConf.NumIntersections.width);
		CopyDefaultCfg(CalibSett, "NumIntersectionsY", Setting::TypeInt, CamCalConf.NumIntersections.height);
		CopyDefaultCfg(CalibSett, "ReprojectionErrorOffset", Setting::TypeFloat, CamCalConf.ReprojectionErrorOffset);
		CopyDefaultCfg(CalibSett, "NumImagePower", Setting::TypeFloat, CamCalConf.NumImagePower);

		CopyDefaultCfg(CalibSett, "SensorSizeX", Setting::TypeFloat, CamCalConf.SensorSize.width);
		CopyDefaultCfg(CalibSett, "SensorSizeY", Setting::TypeFloat, CamCalConf.SensorSize.height);
	}

	cfg.writeFile("../config.cfg");
	
	ConfigInitialised = true;
	
}

RunType GetRunType()
{
	InitConfig();
	return (RunType)ProgramRunType;
}

const aruco::ArucoDetector& GetArucoDetector(){
	if (!HasDetector)
	{
		auto dict = aruco::getPredefinedDictionary(aruco::DICT_4X4_100);
		auto params = aruco::DetectorParameters();
		params.cornerRefinementMethod = GetArucoReduction() == GetFrameSize() ? aruco::CORNER_REFINE_CONTOUR : aruco::CORNER_REFINE_NONE;
		params.useAruco3Detection = true;
		params.adaptiveThreshConstant = 20;
		params.minMarkerPerimeterRate = 0.001;
		auto refparams = aruco::RefineParameters();
		ArucoDet = aruco::ArucoDetector(dict, params, refparams);
	}
	return ArucoDet;
}

void SetNoScreen(bool value)
{
	HasScreenData = value;
	if (value)
	{
		screenresolution = Size(0,0);
		screensize = Size2d(0,0);
	}
}

void GetScreenData()
{
	if (HasScreenData)
	{
		return;
	}
	HasScreenData = true;
	#ifdef WITH_X11
	screenresolution = Size(0,0);
	screensize = Size2d(0,0);
	XScreenSize::Getter sizeGetter;
	auto outputs  = sizeGetter.getOutputs();
	for (int i = 0; i < outputs.size(); i++)
	{
		auto selected = outputs[i];
		if (selected.connection != "connected")
		{
			continue;
		}
		screenresolution.width = selected.width;
		screenresolution.height = selected.height;
		screensize.width = selected.mmWidth;
		screensize.height = selected.mmHeight;
	}
	
	#else
	screenresolution = Size(1920,1080);
	screensize = Size2d(-1,-1);
	#endif
}

Size GetScreenResolution()
{
	GetScreenData();
	return screenresolution;
}

Size2d GetScreenSize()
{
	GetScreenData();
	return screensize;
}

Size GetFrameSize()
{
	InitConfig();
	return CaptureCfg.FrameSize;
}

int GetCaptureFramerate()
{
	InitConfig();
	return (int)CaptureCfg.CaptureFramerate;
}

CameraStartType GetCaptureMethod()
{
	InitConfig();
	return (CameraStartType)CaptureCfg.StartType;
}

CaptureConfig GetCaptureConfig()
{
	InitConfig();
	return CaptureCfg;
}

float GetReductionFactor()
{
	InitConfig();
	return CaptureCfg.ReductionFactor;
}

Size GetArucoReduction()
{
	Size reduction;
	Size basesize = GetFrameSize();
	float reductionFactor = GetReductionFactor();
	reduction = Size(basesize.width / reductionFactor, basesize.height / reductionFactor);
	return reduction;
}

UMat& GetArucoImage(int id)
{
	if (MarkerImages.size() != ARUCO_DICT_SIZE)
	{
		MarkerImages.resize(ARUCO_DICT_SIZE);
	}
	if (MarkerImages[id].empty())
	{
		auto& det = GetArucoDetector();
		auto& dict = det.getDictionary();
		aruco::generateImageMarker(dict, id, 256, MarkerImages[id], 1);
	}
	return MarkerImages[id];
}

vector<InternalCameraConfig>& GetInternalCameraPositionsConfig()
{
	InitConfig();
	return CamerasInternal;
}

const CalibrationConfig& GetCalibrationConfig()
{
	InitConfig();
	return CamCalConf;
}