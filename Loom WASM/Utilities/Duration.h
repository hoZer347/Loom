#pragma once

#include "Clock.h"

#include <algorithm>


namespace Loom
{
	struct Duration final
	{
		/// Resets the clock to a given duration. If negative, truncates to zero.
		void Reset(float newDuration = -1.0f)
		{
			if (newDuration < 0.0f)
				startingDuration = 0.0f;
			else startingDuration = newDuration;

			current = 0.0f;
		};

		/// Advances by the frame's delta, returns true once it is past the duration.
		bool Tick()
		{
			current += Time::DeltaTime();

			return current > startingDuration;
		};

		/// The same tick at a multiple of real time -- 2 runs the clock twice as fast.
		bool Tick(float scale)
		{
			current += Time::DeltaTime() * scale;

			return current > startingDuration;
		};

		/// How far through the duration we are, clamped to 0..1. Zero-length durations
		/// report 1 (already finished).
		float Progress() const
		{
			return startingDuration <= 0.0f
				? 1.0f
				: std::clamp(current / startingDuration, 0.0f, 1.0f);
		};

		/// The starting duration.
		float StartingDuration() const { return startingDuration; };

		Duration() = default;

		Duration(float duration) :
			startingDuration(duration),
			current(0.0f)
		{ };

	private:
		float startingDuration = 0.0f;
		float current = 0.0f;
	};
};
