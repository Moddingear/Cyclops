#include "data/FrameCounter.hpp"

#include <string>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

using namespace cv;

FrameCounter::FrameCounter()
{
	dt = getTickCount();
	startTime = dt;
}

FrameCounter::~FrameCounter()
{

}

double FrameCounter::GetDeltaTime()
{
	int64 dt2 = getTickCount();
	double deltaTime = (double)(dt2-dt) / getTickFrequency();
	dt = dt2;
	return deltaTime;
}

double FrameCounter::GetAbsoluteTime()
{
	int64 dt2 = getTickCount();
	double deltaTime = (double)(dt2-startTime) / getTickFrequency();
	return deltaTime;
}

string FrameCounter::GetFPSString(double DeltaTime)
{
	char buffer[20];
	snprintf(buffer, 20, "fps : %.1f", 1/DeltaTime);
	return string(buffer);
}

void FrameCounter::AddFpsToImage(InputOutputArray img, double DeltaTime)
{
	String strfps = GetFPSString(DeltaTime);
	putText(img, strfps, Point2i(0,img.rows()-20), FONT_HERSHEY_SIMPLEX, 2, Scalar(255, 255, 255), 5);
	putText(img, strfps, Point2i(0,img.rows()-20), FONT_HERSHEY_SIMPLEX, 2, Scalar(0, 0, 0), 2);
}