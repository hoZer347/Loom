#include "doctest.h"

#include "WindowFrame.h"

#ifndef __EMSCRIPTEN__

#include "OpenGL.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <windows.h>
#include <dwmapi.h>

#include <chrono>
#include <vector>

#endif


TEST_SUITE("WindowFrame")
{
	using Hit = Loom::WindowFrame::Hit;
	using Rect = Loom::WindowFrame::Rect;

	constexpr float width = 1600.0f;
	constexpr float height = 900.0f;
	constexpr float border = 4.0f;
	constexpr Rect strip = { 600.0f, 0.0f, 1450.0f, 20.0f };

	Hit At(float x, float y, bool maximized = false)
	{
		return Loom::WindowFrame::HitTest(x, y, width, height, border, maximized, strip);
	};

	TEST_CASE("the edges and corners resize the window")
	{
		CHECK(At(1.0f, 400.0f) == Hit::Left);
		CHECK(At(width - 1.0f, 400.0f) == Hit::Right);
		CHECK(At(800.0f, 1.0f) == Hit::Top);
		CHECK(At(800.0f, height - 1.0f) == Hit::Bottom);
		CHECK(At(1.0f, 1.0f) == Hit::TopLeft);
		CHECK(At(width - 1.0f, 1.0f) == Hit::TopRight);
		CHECK(At(1.0f, height - 1.0f) == Hit::BottomLeft);
		CHECK(At(width - 1.0f, height - 1.0f) == Hit::BottomRight);
	};

	TEST_CASE("the drag strip is the caption, and the rest is the editor's")
	{
		CHECK(At(1000.0f, 10.0f) == Hit::Caption);
		CHECK(At(100.0f, 10.0f) == Hit::Client);
		CHECK(At(1500.0f, 10.0f) == Hit::Client);
		CHECK(At(1000.0f, 400.0f) == Hit::Client);
	};

	TEST_CASE("a maximized window has no edges to resize by")
	{
		CHECK(At(1.0f, 400.0f, true) == Hit::Client);
		CHECK(At(1000.0f, 1.0f, true) == Hit::Caption);
		CHECK(At(width - 1.0f, 1.0f, true) == Hit::Client);
	};

	TEST_CASE("an empty strip drags nothing")
	{
		CHECK(Loom::WindowFrame::HitTest(1000.0f, 10.0f, width, height, border, false, { }) == Hit::Client);
	};
};

#ifndef __EMSCRIPTEN__

TEST_SUITE("WindowFrame")
{
	constexpr int window_width = 320;
	constexpr int window_height = 240;
	constexpr std::chrono::milliseconds held_for{ 200 };
	constexpr std::chrono::milliseconds nested_for{ 50 };

	int s_redraws = 0;
	int s_depth = 0;
	int s_deepest = 0;
	int s_nestedTimers = 0;
	HWND s_window = nullptr;

	// Messages for window until length has passed, as the OS's own loop dispatches them.
	int Pump(HWND window, std::chrono::milliseconds length)
	{
		int timers = 0;
		const auto end = std::chrono::steady_clock::now() + length;

		while (std::chrono::steady_clock::now() < end)
		{
			MSG message;

			while (PeekMessageW(&message, window, 0, 0, PM_REMOVE))
			{
				if (message.message == WM_TIMER)
					timers++;

				DispatchMessageW(&message);
			};

			Sleep(1);
		};

		return timers;
	};

	void Count()
	{
		s_redraws++;
	};

	struct HiddenWindow
	{
		GLFWwindow* window = nullptr;
		HWND hwnd = nullptr;

		HiddenWindow()
		{
			glfwInit();
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			window = glfwCreateWindow(window_width, window_height, "WindowFrame", nullptr, nullptr);
			hwnd = glfwGetWin32Window(window);
			s_window = hwnd;
			s_redraws = 0;
			s_depth = 0;
			s_deepest = 0;
			s_nestedTimers = 0;
		};

		~HiddenWindow()
		{
			glfwDefaultWindowHints();
			glfwDestroyWindow(window);
			glfwTerminate();
		};
	};

	void PumpingRedraw()
	{
		s_depth++;
		s_deepest = s_depth > s_deepest ? s_depth : s_deepest;
		s_redraws++;
		s_nestedTimers += Pump(s_window, nested_for);
		s_depth--;
	};

	TEST_CASE_FIXTURE(HiddenWindow, "a window being sized redraws on a timer until let go")
	{
		Loom::WindowFrame frame;
		frame.Install(window, Count);

		SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
		Pump(hwnd, held_for);
		CHECK(s_redraws > 0);

		SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
		Pump(hwnd, nested_for);
		const int redraws = s_redraws;
		Pump(hwnd, held_for);
		CHECK(s_redraws == redraws);
	};

	TEST_CASE_FIXTURE(HiddenWindow, "a window being sized redraws at each new size before the OS goes on")
	{
		constexpr int grown = 16;
		constexpr UINT size_only = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE;
		constexpr UINT move_only = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;

		Loom::WindowFrame frame;
		frame.Install(window, Count);

		SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
		SetWindowPos(hwnd, nullptr, 0, 0, window_width, window_height + grown, size_only);
		CHECK(s_redraws == 1);

		SetWindowPos(hwnd, nullptr, grown, grown, 0, 0, move_only);
		CHECK(s_redraws == 1);

		SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
		SetWindowPos(hwnd, nullptr, 0, 0, window_width, window_height, size_only);
		CHECK(s_redraws == 1);
	};

	std::vector<std::chrono::steady_clock::time_point> s_redrawnAt;

	void Stamp()
	{
		s_redrawnAt.push_back(std::chrono::steady_clock::now());
	};

	TEST_CASE_FIXTURE(HiddenWindow, "a window being sized takes each new size only once the screen is next composed")
	{
		constexpr int sizes = 6;
		constexpr UINT size_only = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE;
		constexpr double least_of_period = 0.5;

		DWM_TIMING_INFO timing{ };
		timing.cbSize = sizeof(timing);
		REQUIRE(SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timing)));

		LARGE_INTEGER frequency{ };
		QueryPerformanceFrequency(&frequency);
		REQUIRE(frequency.QuadPart > 0);

		const std::chrono::duration<double> period((double)timing.qpcRefreshPeriod / (double)frequency.QuadPart);

		Loom::WindowFrame frame;
		frame.Install(window, Stamp);
		s_redrawnAt.clear();

		SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);

		for (int size = 1; size <= sizes; size++)
			SetWindowPos(hwnd, nullptr, 0, 0, window_width, window_height + size, size_only);

		SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);

		REQUIRE(s_redrawnAt.size() == sizes);

		for (size_t redraw = 1; redraw < s_redrawnAt.size(); redraw++)
			CHECK(s_redrawnAt[redraw] - s_redrawnAt[redraw - 1] > period * least_of_period);
	};

	TEST_CASE_FIXTURE(HiddenWindow, "a redraw that pumps messages is not redrawn inside itself")
	{
		Loom::WindowFrame frame;
		frame.Install(window, PumpingRedraw);

		SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
		Pump(hwnd, held_for);
		SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);

		CHECK(s_redraws > 0);
		CHECK(s_nestedTimers > 0);
		CHECK(s_deepest == 1);
	};

	TEST_CASE_FIXTURE(HiddenWindow, "no timer is set for a frame given no redraw")
	{
		Loom::WindowFrame frame;
		frame.Install(window, nullptr);

		SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
		Sleep((DWORD)nested_for.count());

		MSG message;
		CHECK_FALSE(PeekMessageW(&message, hwnd, WM_TIMER, WM_TIMER, PM_NOREMOVE));

		SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
	};
};

#endif
