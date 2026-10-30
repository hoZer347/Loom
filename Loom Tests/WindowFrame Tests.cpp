#include "doctest.h"

#include "WindowFrame.h"

#ifdef _WIN32

#include "OpenGL.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <windows.h>

#include <chrono>
#include <memory>
#include <vector>

#elif !defined(__EMSCRIPTEN__)

#include "OpenGL.h"

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

#ifdef _WIN32

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

			while (std::chrono::steady_clock::now() < end && PeekMessageW(&message, window, 0, 0, PM_REMOVE))
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

	TEST_CASE_FIXTURE(HiddenWindow, "a window being moved or sized redraws at each new place and size before the OS goes on")
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
		CHECK(s_redraws == 2);

		SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
		SetWindowPos(hwnd, nullptr, 0, 0, window_width, window_height, size_only);
		CHECK(s_redraws == 2);
	};

	TEST_CASE_FIXTURE(HiddenWindow, "a window held still redraws over and over, and its timer still comes between them")
	{
		// Far over the one redraw a timer alone gives, and far under what a hidden window manages.
		constexpr int redraws_to_a_timer = 4;

		Loom::WindowFrame frame;
		frame.Install(window, Count);

		SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
		const int timers = Pump(hwnd, held_for);
		SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);

		REQUIRE(timers > 0);
		CHECK(s_redraws >= timers * redraws_to_a_timer);
	};

	void CountAndPost()
	{
		s_redraws++;
		PostMessageW(s_window, WM_NULL, 0, 0);
	};

	TEST_CASE_FIXTURE(HiddenWindow, "a window held with a message always waiting leaves its frames to the timer")
	{
		Loom::WindowFrame frame;
		frame.Install(window, CountAndPost);

		SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
		const int timers = Pump(hwnd, held_for);
		SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);

		REQUIRE(timers > 0);
		CHECK(s_redraws <= timers + 1);
	};

	std::unique_ptr<Loom::WindowFrame> s_doomed;

	void CountThenDestroy()
	{
		s_redraws++;
		s_doomed.reset();
	};

	TEST_CASE_FIXTURE(HiddenWindow, "a window's frame taken away by a redraw while held lets the window go")
	{
		s_doomed = std::make_unique<Loom::WindowFrame>();
		s_doomed->Install(window, CountThenDestroy);

		SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
		Pump(hwnd, held_for);

		constexpr UINT move_only = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;

		CHECK(s_doomed == nullptr);
		CHECK(s_redraws == 1);

		Loom::WindowFrame next;
		next.Install(window, Count);
		SetWindowPos(hwnd, nullptr, 1, 1, 0, 0, move_only);
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

		// Far under any screen's refresh, and far over a size taken without waiting for one.
		constexpr std::chrono::milliseconds least_wait{ 1 };

		Loom::WindowFrame frame;
		frame.Install(window, Stamp);
		s_redrawnAt.clear();

		SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);

		for (int size = 1; size <= sizes; size++)
			SetWindowPos(hwnd, nullptr, 0, 0, window_width, window_height + size, size_only);

		SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);

		REQUIRE(s_redrawnAt.size() == sizes);
		CHECK(s_redrawnAt.back() - s_redrawnAt.front() > least_wait * (sizes - 1));
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

#elif !defined(__EMSCRIPTEN__)

TEST_SUITE("WindowFrame")
{
	constexpr int window_width = 320;
	constexpr int window_height = 240;
	constexpr Loom::WindowFrame::Rect strip = { 0.0f, 0.0f, 320.0f, 20.0f };

	int s_presses = 0;

	void Count(GLFWwindow*, int, int, int)
	{
		s_presses++;
	};

	void Moved(GLFWwindow*, double, double)
	{
	};

	void Entered(GLFWwindow*, int)
	{
	};

	struct HiddenWindow
	{
		GLFWwindow* window = nullptr;

		HiddenWindow()
		{
			glfwInit();
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			window = glfwCreateWindow(window_width, window_height, "WindowFrame", nullptr, nullptr);
			glfwSetMouseButtonCallback(window, Count);
			s_presses = 0;
		};

		~HiddenWindow()
		{
			glfwDefaultWindowHints();
			glfwDestroyWindow(window);
			glfwTerminate();
		};

		// A left press with the mouse at x, y, as the window would get it. GLFW
		// moves the mouse only in a focused window, so the window moves instead.
		void PressAt(double x, double y)
		{
			double mouse_x = 0.0;
			double mouse_y = 0.0;
			int left = 0;
			int top = 0;

			glfwGetCursorPos(window, &mouse_x, &mouse_y);
			glfwGetWindowPos(window, &left, &top);
			glfwSetWindowPos(window, left + (int)(mouse_x - x), top + (int)(mouse_y - y));
			glfwGetCursorPos(window, &mouse_x, &mouse_y);
			REQUIRE(mouse_x == x);
			REQUIRE(mouse_y == y);

			Send(GLFW_PRESS);
		};

		void Send(int action)
		{
			const GLFWmousebuttonfun installed = glfwSetMouseButtonCallback(window, nullptr);

			glfwSetMouseButtonCallback(window, installed);
			installed(window, GLFW_MOUSE_BUTTON_LEFT, action, 0);
		};

		// The mouse moving to x, y, as the window would hear of it.
		void MoveTo(double x, double y)
		{
			const GLFWcursorposfun installed = glfwSetCursorPosCallback(window, nullptr);

			glfwSetCursorPosCallback(window, installed);
			installed(window, x, y);
		};
	};

	TEST_CASE_FIXTURE(HiddenWindow, "a frame takes the title bar off")
	{
		REQUIRE(glfwGetPlatform() == GLFW_PLATFORM_X11);

		Loom::WindowFrame frame;
		frame.Install(window, nullptr);

		CHECK(frame.IsInstalled());
		CHECK(glfwGetWindowAttrib(window, GLFW_DECORATED) == GLFW_FALSE);
	};

	TEST_CASE_FIXTURE(HiddenWindow, "a press on the editor reaches the callback before the frame's")
	{
		Loom::WindowFrame frame;
		frame.Install(window, nullptr);
		frame.SetDragStrip(strip);

		PressAt(window_width / 2.0, window_height / 2.0);
		CHECK(s_presses == 1);
	};

	TEST_CASE_FIXTURE(HiddenWindow, "a press on the drag strip or an edge never reaches the editor")
	{
		constexpr double inside_strip = 10.0;
		constexpr double inside_edge = 1.0;

		Loom::WindowFrame frame;
		frame.Install(window, nullptr);
		frame.SetDragStrip(strip);

		PressAt(window_width / 2.0, inside_strip);
		PressAt(inside_edge, window_height / 2.0);
		CHECK(s_presses == 0);
	};

	TEST_CASE_FIXTURE(HiddenWindow, "a click on the drag strip lets go without reaching the editor")
	{
		constexpr double inside_strip = 10.0;

		Loom::WindowFrame frame;
		frame.Install(window, nullptr);
		frame.SetDragStrip(strip);

		PressAt(window_width / 2.0, inside_strip);
		Send(GLFW_RELEASE);
		CHECK(s_presses == 0);
	};

	// A release reaches the editor only once the window manager has taken the drag.
	TEST_CASE_FIXTURE(HiddenWindow, "a press on the drag strip holds the window while the mouse stays near")
	{
		constexpr double inside_strip = 10.0;
		constexpr double short_of_drag = 3.0;
		const double middle = window_width / 2.0;

		Loom::WindowFrame frame;
		frame.Install(window, nullptr);
		frame.SetDragStrip(strip);

		PressAt(middle, inside_strip);
		MoveTo(middle + short_of_drag, inside_strip);
		Send(GLFW_RELEASE);
		CHECK(s_presses == 0);
	};

	TEST_CASE_FIXTURE(HiddenWindow, "a press on the drag strip moves the window once dragged far enough")
	{
		constexpr double inside_strip = 10.0;
		constexpr double past_drag = 20.0;
		const double middle = window_width / 2.0;

		Loom::WindowFrame frame;
		frame.Install(window, nullptr);
		frame.SetDragStrip(strip);

		PressAt(middle, inside_strip);
		MoveTo(middle + past_drag, inside_strip);
		Send(GLFW_RELEASE);
		CHECK(s_presses == 1);
	};

	TEST_CASE_FIXTURE(HiddenWindow, "a second click on the drag strip is a double-click, not a drag")
	{
		constexpr double inside_strip = 10.0;
		constexpr double past_drag = 20.0;
		const double middle = window_width / 2.0;

		Loom::WindowFrame frame;
		frame.Install(window, nullptr);
		frame.SetDragStrip(strip);

		PressAt(middle, inside_strip);
		Send(GLFW_RELEASE);
		PressAt(middle, inside_strip);
		MoveTo(middle + past_drag, inside_strip);
		CHECK(frame.Hovered() == Loom::WindowFrame::Hit::Caption);

		// Nothing is held after a double-click, so its release goes on to the editor.
		Send(GLFW_RELEASE);
		CHECK(s_presses == 1);
	};

	TEST_CASE_FIXTURE(HiddenWindow, "the edge under the mouse is kept from its last move")
	{
		constexpr double inside_edge = 1.0;

		Loom::WindowFrame frame;
		frame.Install(window, nullptr);

		CHECK(frame.Hovered() == Loom::WindowFrame::Hit::Client);
		MoveTo(inside_edge, window_height / 2.0);
		CHECK(frame.Hovered() == Loom::WindowFrame::Hit::Left);
	};

	TEST_CASE_FIXTURE(HiddenWindow, "a frame gives the window its callbacks back when it goes")
	{
		glfwSetCursorPosCallback(window, Moved);
		glfwSetCursorEnterCallback(window, Entered);

		{
			Loom::WindowFrame frame;
			frame.Install(window, nullptr);
		};

		CHECK(glfwSetMouseButtonCallback(window, nullptr) == Count);
		CHECK(glfwSetCursorPosCallback(window, nullptr) == Moved);
		CHECK(glfwSetCursorEnterCallback(window, nullptr) == Entered);
	};
};

#endif
