#pragma once

#include "Clock.h"
#include "Gait.h"

#include "glm/glm.hpp"


namespace Loom
{
	/// Carries a velocity and pushes a position around with it.
	///
	/// Moves whatever position vector it is handed, in world space, so it works the same
	/// on a GameObject's transform and on anything else that carries its own position.
	struct SpeedManager final
	{
		const glm::vec3& Velocity() const { return _velocity; };

		float Speed() const { return glm::length(_velocity); };

		void Reset() { _velocity = glm::vec3(0.0f); };

		void ApplyAcceleration(glm::vec3& position, const glm::vec3& direction, const Gait& gait)
		{
			const glm::vec3 target =
				ClampMagnitude(direction, 1.0f)
				* gait.MaxSpeed();

			_velocity = MoveTowards(
				_velocity,
				target,
				gait.Acceleration()
				* Time::DeltaTime());

			Move(position);
		};

		void ApplyFriction(glm::vec3& position, float friction)
		{
			_velocity = MoveTowards(
				_velocity,
				glm::vec3(0.0f),
				friction
				* Time::DeltaTime());

			Move(position);
		};

		/// Shortens the vector to at most max_length, leaving a shorter one alone, which is
		/// what an analogue stick's read wants.
		static glm::vec3 ClampMagnitude(const glm::vec3& value, float max_length)
		{
			const float length = glm::length(value);

			if (length <= max_length)
				return value;

			return value / length * max_length;
		};

		/// Steps from current towards target by at most max_delta, and never overshoots.
		/// The step is an absolute distance rather than a lerp's fraction, so an
		/// acceleration stays an acceleration at any framerate.
		static glm::vec3 MoveTowards(const glm::vec3& current, const glm::vec3& target, float max_delta)
		{
			const glm::vec3 difference = target - current;
			const float distance = glm::length(difference);

			if (distance <= max_delta || distance == 0.0f)
				return target;

			return current + difference / distance * max_delta;
		};

	private:
		void Move(glm::vec3& position)
		{
			position += _velocity * Time::DeltaTime();
		};

		glm::vec3 _velocity{ 0.0f };
	};
};
