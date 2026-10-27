#include "WindowFrame.h"

#include <windows.h>
#include <commctrl.h>

#include "OpenGL.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#pragma comment(lib, "comctl32.lib")


namespace Loom
{
	namespace
	{
		constexpr UINT_PTR SUBCLASS_ID = 0x4C4F4F4D;

		// What a title bar gives a window: snapping, the maximize animation
		// and the shadow, all of which go with a bare popup.
		constexpr LONG FRAMED_STYLE = WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX;

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

		LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data)
		{
			const WindowFrame& frame = *(const WindowFrame*)data;
			const bool framed = (GetWindowLongW(window, GWL_STYLE) & WS_THICKFRAME) != 0;

			// A maximized window hangs its frame off the monitor's edges.
			if (message == WM_NCCALCSIZE && wparam && framed)
			{
				if (IsZoomed(window))
				{
					const int padding = GetSystemMetrics(SM_CXPADDEDBORDER);
					RECT& client = ((NCCALCSIZE_PARAMS*)lparam)->rgrc[0];

					InflateRect(&client, -(GetSystemMetrics(SM_CXSIZEFRAME) + padding), -(GetSystemMetrics(SM_CYSIZEFRAME) + padding));
				};

				return 0;
			};

			if (message == WM_NCHITTEST && framed)
			{
				POINT point = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
				RECT client{ };

				ScreenToClient(window, &point);
				GetClientRect(window, &client);

				const float border = (float)GetSystemMetrics(SM_CXSIZEFRAME);
				const WindowFrame::Hit hit = WindowFrame::HitTest((float)point.x, (float)point.y, (float)client.right, (float)client.bottom, border, IsZoomed(window) != FALSE, frame.DragStrip());

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

	void WindowFrame::Install(GLFWwindow* window)
	{
		if (window == nullptr || m_window != nullptr)
			return;

		if (!SetWindowSubclass(glfwGetWin32Window(window), Procedure, SUBCLASS_ID, (DWORD_PTR)this))
			return;

		m_window = window;

		// GLFW then sizes the window as though it had no frame, which with the
		// whole of it client area is what it has.
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
