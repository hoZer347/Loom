#include "doctest.h"

#include "WindowFullscreen.h"


TEST_SUITE("WindowFullscreen")
{
	using Rect = Loom::WindowFullscreen::Rect;

	constexpr Rect left_monitor = { 0, 0, 1920, 1080 };
	constexpr Rect right_monitor = { 1920, 0, 2560, 1440 };

	TEST_CASE("the window goes to the monitor it covers most of")
	{
		const std::vector<Rect> monitors = { left_monitor, right_monitor };

		CHECK(Loom::WindowFullscreen::MonitorUnder({ 100, 100, 800, 600 }, monitors) == 0);
		CHECK(Loom::WindowFullscreen::MonitorUnder({ 2000, 100, 800, 600 }, monitors) == 1);

		// Straddling the seam, more of it on the right.
		CHECK(Loom::WindowFullscreen::MonitorUnder({ 1700, 100, 800, 600 }, monitors) == 1);
	};

	TEST_CASE("a window on no monitor goes to the first, and with none there is nowhere")
	{
		CHECK(Loom::WindowFullscreen::MonitorUnder({ -5000, -5000, 800, 600 }, { left_monitor, right_monitor }) == 0);
		CHECK(Loom::WindowFullscreen::MonitorUnder({ 0, 0, 800, 600 }, { }) == -1);
	};

	// A hidden run lays out as though it filled the monitor, and moves no window.
	TEST_CASE("without a window it is on and off all the same")
	{
		Loom::WindowFullscreen fullscreen;

		fullscreen.Enter(nullptr);
		CHECK(fullscreen.IsOn());

		fullscreen.Leave(nullptr);
		CHECK_FALSE(fullscreen.IsOn());
	};
};
