#pragma once


namespace Loom
{
	/// Tuning for one movement style -- how fast it goes and how hard it accelerates.
	struct Gait final
	{
		Gait() = default;

		Gait(float maxSpeed, float acceleration) :
			maxSpeed(maxSpeed),
			acceleration(acceleration)
		{ };

		float MaxSpeed() const { return maxSpeed; };
		float Acceleration() const { return acceleration; };

		void SetMaxSpeed(float value) { maxSpeed = value; };
		void SetAcceleration(float value) { acceleration = value; };

	private:
		float maxSpeed = 0.0f;
		float acceleration = 0.0f;
	};
};
