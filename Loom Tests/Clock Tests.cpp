#include "doctest.h"

#include "Utilities/Clock.h"

#include "OpenGL.h"

#include <chrono>
#include <thread>


// The web's clock is the browser's, which runs without glfwInit and cannot be
// slept on here.
#ifndef __EMSCRIPTEN__
TEST_SUITE("Clock")
{
	constexpr float default_max_delta = 0.1f;

	// The clock reads GLFW's time, which is only kept once GLFW is up.
	struct RunningClock
	{
		RunningClock()
		{
			glfwInit();
			Loom::Utilities::Tick();
		};

		~RunningClock()
		{
			Loom::Time::SetScale(1.0f);
			Loom::Time::SetMaxDelta(default_max_delta);
			glfwTerminate();
		};

		// A frame that takes at least this long.
		static void Frame(std::chrono::milliseconds length)
		{
			std::this_thread::sleep_for(length);
			Loom::Utilities::Tick();
		};
	};

	constexpr std::chrono::milliseconds frame_length{ 20 };

	TEST_CASE_FIXTURE(RunningClock, "a stopped clock still says how long the frame took, unscaled")
	{
		Loom::Time::SetScale(0.0f);
		Frame(frame_length);

		CHECK(Loom::Time::DeltaTime() == 0.0f);
		CHECK(Loom::Time::UnscaledDeltaTime() > 0.0f);
	};

	TEST_CASE_FIXTURE(RunningClock, "the scale multiplies the frame and leaves the unscaled one alone")
	{
		constexpr float half = 0.5f;

		Loom::Time::SetScale(half);
		Frame(frame_length);

		CHECK(Loom::Time::DeltaTime() == doctest::Approx(Loom::Time::UnscaledDeltaTime() * half));
	};

	TEST_CASE_FIXTURE(RunningClock, "the unscaled frame is clamped as the scaled one is")
	{
		constexpr float short_max_delta = 0.005f;

		Loom::Time::SetMaxDelta(short_max_delta);
		Frame(frame_length);

		CHECK(Loom::Time::UnscaledDeltaTime() == short_max_delta);
	};
};
#endif
