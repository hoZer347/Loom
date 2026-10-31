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

	TEST_CASE("a placement survives the command line, off to the left of the primary monitor")
	{
		const Loom::WindowFullscreen::Placement placement = { { -1783, 292, 1111, 677 }, true, true, 1 };
		const auto read = Loom::WindowFullscreen::Read(Loom::WindowFullscreen::Write(placement));

		REQUIRE(read.has_value());
		CHECK(read->windowed.x == -1783);
		CHECK(read->windowed.y == 292);
		CHECK(read->windowed.width == 1111);
		CHECK(read->windowed.height == 677);
		CHECK(read->maximized);
		CHECK(read->minimized);
		CHECK(read->view == 1);
	};

	TEST_CASE("a window out of fullscreen reads back as not fullscreen")
	{
		const auto read = Loom::WindowFullscreen::Read(Loom::WindowFullscreen::Write({ { 10, 20, 800, 600 } }));

		REQUIRE(read.has_value());
		CHECK_FALSE(read->maximized);
		CHECK_FALSE(read->minimized);
		CHECK(read->view == Loom::WindowFullscreen::no_view);
	};

	TEST_CASE("anything else is no placement")
	{
		CHECK_FALSE(Loom::WindowFullscreen::Read("").has_value());
		CHECK_FALSE(Loom::WindowFullscreen::Read("10,20,800,600").has_value());
		CHECK_FALSE(Loom::WindowFullscreen::Read("10,20,800,600,0,0,-1,5").has_value());
		CHECK_FALSE(Loom::WindowFullscreen::Read("10,20,800,600,0,0,-1x").has_value());
		CHECK_FALSE(Loom::WindowFullscreen::Read("10,20,0,600,0,0,-1").has_value());
		CHECK_FALSE(Loom::WindowFullscreen::Read("10,20,800,-600,0,0,-1").has_value());
		CHECK_FALSE(Loom::WindowFullscreen::Read("10,20,800,600,0,0,-2").has_value());
	};

	TEST_CASE("a window maximized where it sits keeps its place")
	{
		const Rect windowed = { 2000, 100, 800, 600 };
		const Rect carried = Loom::WindowFullscreen::Carried(windowed, right_monitor, { left_monitor, right_monitor });

		CHECK(carried.x == windowed.x);
		CHECK(carried.y == windowed.y);
	};

	TEST_CASE("a maximized window sent to another monitor takes its place along")
	{
		const Rect carried = Loom::WindowFullscreen::Carried({ 100, 100, 800, 600 }, right_monitor, { left_monitor, right_monitor });

		CHECK(carried.x == 100 + right_monitor.x);
		CHECK(carried.y == 100);
		CHECK(carried.width == 800);
		CHECK(carried.height == 600);
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
