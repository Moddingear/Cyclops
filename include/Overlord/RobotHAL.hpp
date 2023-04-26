#pragma once


#include <vector>

namespace Overlord
{

	class LinearMovement
	{
	public:
		double Pos=0, Speed=0, Acceleration=1, Deceleration=2;
		double MaxSpeed=1, MinSpeed=0;
		double TargetPos;

		LinearMovement()
		{};

		LinearMovement(double acceleration, double deceleration, double maxspeed, double minspeed, double pos0=0.0, double target0=0.0, double speed0=0.0)
			:Pos(pos0), Speed(speed0), Acceleration(acceleration), Deceleration(deceleration), MaxSpeed(maxspeed), MinSpeed(minspeed), TargetPos(target0)
		{};

		virtual ~LinearMovement() {};

		static double wraptwopi(double in);

		static double closestangle(double x, double ref);

		constexpr double SpeedDeltaTime(double v0, double v1, double acc)
		{
			return (v1-v0)/acc;
		}

		constexpr double SpeedDeltaDistance(double v0, double v1, double acc)
		{
			double deltaspeed = v1-v0;
			return deltaspeed/acc*(deltaspeed*0.5 + v0);
		};

		enum class MoveABResult
		{
			Done,
			Overshot,
			WrongDirection,
			NoTimeLeft
		};

		double GetBrakingDistance(double v0);
		//Must be going the right direction, will only do a single acceleration/deceleration
		MoveABResult MoveAB(double Target, double& TimeBudget);

		void SetTarget(double NewTarget);

		void SetTargetAngular(double angle);

		//Try to move to the target pos for dt maximum time.
		//Returns the time used
		double Tick(double &TimeBudget);
	};


	class RobotHAL
	{
	public:
		double posX=0, posY=0;
		LinearMovement PositionLinear;
		LinearMovement Rotation;
		LinearMovement ClawHeight, ClawExtension;
		LinearMovement Trays[3];

		RobotHAL(/* args */);
		RobotHAL(RobotHAL* CopyFrom);
		virtual ~RobotHAL();

		virtual void GetForwardVector(double &x, double &y);

		virtual void GetStoppingPosition(double &endX, double &endY);

		virtual double Rotate(double target, double &TimeBudget);

		//positive distance means forwards, negative means backwards
		virtual double LinearMove(double distance, double &TimeBudget);

		//move to position, no rotation target
		virtual double MoveTo(double x, double y, double &TimeBudget);

		//move to position with rotation target
		virtual double MoveTo(double x, double y, double rot, double &TimeBudget);

		virtual double MoveClaw(double height, double extension);

		virtual double MoveTray(int index, double extension);
	};

} // namespace overlord