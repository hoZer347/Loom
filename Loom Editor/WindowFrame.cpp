#include "WindowFrame.h"

// The editor only runs on Windows, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "OpenGL.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <windows.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

#endif


namespace Loom
{
	WindowFrame::Hit WindowFrame::HitTest(float x, float y, float width, float height, float border, bool maximized, const Rect& strip)
	{
		if (!maximized)
		{
			const bool left = x < border;
			const bool right = x >= width - border;
			const bool top = y < border;
			const bool bottom = y >= height - border;

			if (top && left)
				return Hit::TopLeft;

			if (top && right)
				return Hit::TopRight;

			if (bottom && left)
				return Hit::BottomLeft;

			if (bottom && right)
				return Hit::BottomRight;

			if (left)
				return Hit::Left;

			if (right)
				return Hit::Right;

			if (top)
				return Hit::Top;

			if (bottom)
				return Hit::Bottom;
		};

		if (x >= strip.left && x < strip.right && y >= strip.top && y < strip.bottom)
			return Hit::Caption;

		return Hit::Client;
	};
};

#ifndef __EMSCRIPTEN__

namespace Loom
{
	namespace
	{
		// Tags this subclass on the window, among any others it has.
		constexpr UINT_PTR SUBCLASS_ID = 0x4C4F4F4D;

		// Snapping, the maximize animation and the shadow all go with a bare popup.
		constexpr LONG FRAMED_STYLE = WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX;

		// The one window the OS is moving or sizing, which a thread has at most one of.
		HWND s_held = nullptr;

		// Not inside another redraw, as one that pumps messages would be.
		void RedrawOnce(const WindowFrame& frame)
		{
			static bool redrawing = false;

			if (redrawing)
				return;

			redrawing = true;
			frame.Redrawing()();
			redrawing = false;
		};

		LRESULT HitCode(WindowFrame::Hit hit)
		{
			switch (hit)
			{
			case WindowFrame::Hit::Caption: return HTCAPTION;
			case WindowFrame::Hit::Left: return HTLEFT;
			case WindowFrame::Hit::Right: return HTRIGHT;
			case WindowFrame::Hit::Top: return HTTOP;
			case WindowFrame::Hit::Bottom: return HTBOTTOM;
			case WindowFrame::Hit::TopLeft: return HTTOPLEFT;
			case WindowFrame::Hit::TopRight: return HTTOPRIGHT;
			case WindowFrame::Hit::BottomLeft: return HTBOTTOMLEFT;
			case WindowFrame::Hit::BottomRight: return HTBOTTOMRIGHT;
			default: return HTCLIENT;
			};
		};

		// Windows 10's per-monitor calls, which the Windows 7 target leaves undeclared.
		int Metric(HWND window, int index)
		{
			using DpiForWindow = UINT(WINAPI*)(HWND);
			using MetricForDpi = int(WINAPI*)(int, UINT);

			static const HMODULE user32 = GetModuleHandleW(L"user32.dll");
			static const DpiForWindow dpi_for_window = (DpiForWindow)GetProcAddress(user32, "GetDpiForWindow");
			static const MetricForDpi metric_for_dpi = (MetricForDpi)GetProcAddress(user32, "GetSystemMetricsForDpi");

			if (dpi_for_window == nullptr || metric_for_dpi == nullptr)
				return GetSystemMetrics(index);

			return metric_for_dpi(index, dpi_for_window(window));
		};

		LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data)
		{
			const WindowFrame& frame = *(const WindowFrame*)data;
			const bool framed = (GetWindowLongW(window, GWL_STYLE) & WS_THICKFRAME) != 0;

			// A maximized window hangs its frame off the monitor's edges.
			if (message == WM_NCCALCSIZE && wparam && framed)
			{
				if (IsZoomed(window))
				{
					const int padding = Metric(window, SM_CXPADDEDBORDER);
					RECT& client = ((NCCALCSIZE_PARAMS*)lparam)->rgrc[0];

					InflateRect(
						&client,
						-(Metric(window, SM_CXSIZEFRAME) + padding),
						-(Metric(window, SM_CYSIZEFRAME) + padding));
				};

				return 0;
			};

			// The OS holds the frame loop while it moves or sizes the window, so frames come from a timer meanwhile.
			if (message == WM_ENTERSIZEMOVE && frame.Redrawing() != nullptr)
			{
				s_held = window;
				SetTimer(window, SUBCLASS_ID, USER_TIMER_MINIMUM, nullptr);
			};

			if (message == WM_EXITSIZEMOVE)
			{
				if (s_held == window)
					s_held = nullptr;

				KillTimer(window, SUBCLASS_ID);
			};

			// The timer only comes once the mouse is still, so each new size is drawn before the OS takes the next.
			if (message == WM_WINDOWPOSCHANGED && window == s_held && (((const WINDOWPOS*)lparam)->flags & SWP_NOSIZE) == 0)
			{
				const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
				RedrawOnce(frame);
				return result;
			};

			if (message == WM_TIMER && wparam == SUBCLASS_ID)
			{
				RedrawOnce(frame);
				return 0;
			};

			if (message == WM_NCHITTEST && framed)
			{
				POINT point = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
				RECT client{ };

				ScreenToClient(window, &point);
				GetClientRect(window, &client);

				const WindowFrame::Hit hit = WindowFrame::HitTest(
					(float)point.x,
					(float)point.y,
					(float)client.right,
					(float)client.bottom,
					(float)Metric(window, SM_CXSIZEFRAME),
					IsZoomed(window) != FALSE,
					frame.DragStrip());

				return HitCode(hit);
			};

			return DefSubclassProc(window, message, wparam, lparam);
		};
	};

	WindowFrame::~WindowFrame()
	{
		if (m_window != nullptr)
			RemoveWindowSubclass(glfwGetWin32Window(m_window), Procedure, SUBCLASS_ID);
	};

	void WindowFrame::Install(GLFWwindow* window, Redraw redraw)
	{
		if (window == nullptr || m_window != nullptr)
			return;

		if (!SetWindowSubclass(glfwGetWin32Window(window), Procedure, SUBCLASS_ID, (DWORD_PTR)this))
			return;

		m_window = window;
		m_redraw = redraw;

		// The whole window is client area, so GLFW sizes it as frameless.
		glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
		Fit();
	};

	void WindowFrame::Fit()
	{
		if (m_window == nullptr)
			return;

		const HWND hwnd = glfwGetWin32Window(m_window);
		const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
		const LONG fitted = glfwGetWindowMonitor(m_window) != nullptr ? style & ~FRAMED_STYLE : style | FRAMED_STYLE;

		if (fitted == style)
			return;

		SetWindowLongW(hwnd, GWL_STYLE, fitted);
		SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
	};
};

#endif
