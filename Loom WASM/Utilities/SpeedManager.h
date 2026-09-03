#pragma once

#include "Clock.h"
#include "Gait.h"

#include "Vector.h"


namespace Loom
{
	using Vec3 = Math::vec3<float>;

	/// Carries a velocity and pushes a position around with it.
	///
	/// Moves a position vector directly in world space, GameObject having no transform
	/// component to push around.
	struct SpeedManager final
	{
		const Vec3& Velocity() const { return _velocity; };

		float Speed() const { return _velocity.Magnitude(); };

		void Reset() { _velocity = Vec3(); };

		void ApplyAcceleration(Vec3& position, const Vec3& direction, const Gait& gait)
		{
			const Vec3 target =
				Vec3::ClampMagnitude(direction, 1.0f)
				* gait.MaxSpeed();

			_velocity = Vec3::MoveTowards(
				_velocity,
				target,
				gait.Acceleration()
				* Time::DeltaTime());

			Move(position);
		};

		void ApplyFriction(Vec3& position, float friction)
		{
			_velocity = Vec3::MoveTowards(
				_velocity,
				Vec3::Zero(),
				friction
				* Time::DeltaTime());

			Move(position);
		};

	private:
		void Move(Vec3& position)
		{
			position += _velocity * Time::DeltaTime();
		};

		Vec3 _velocity{ };
	};
};
