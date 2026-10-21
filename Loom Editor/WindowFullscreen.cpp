#include "WindowFullscreen.h"

#include "OpenGL.h"

#include <algorithm>


namespace Loom
{
	int WindowFullscreen::MonitorUnder(const Rect& window, const std::vector<Rect>& monitors)
	{
		int best = monitors.empty() ? -1 : 0;
		long long most = 0;

		for (int i = 0; i < (int)monitors.size(); i++)
		{
			const Rect& monitor = monitors[i];
			const long long across = std::min(window.x + window.width, monitor.x + monitor.width) - std::max(window.x, monitor.x);
			const long long down = std::min(window.y + window.height, monitor.y + monitor.height) - std::max(window.y, monitor.y);

			if (across > 0 && down > 0 && across * down > most)
			{
				most = across * down;
				best = i;
			};
		};

		return best;
	};

	void WindowFullscreen::Enter(GLFWwindow* window)
	{
		if (m_on)
			return;

		m_on = true;
		m_moved = false;

		if (window == nullptr || !glfwGetWindowAttrib(window, GLFW_VISIBLE))
			return;

		int count = 0;
		GLFWmonitor** monitors = glfwGetMonitors(&count);
		std::vector<Rect> rects;

		for (int i = 0; i < count; i++)
		{
			const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
			Rect rect;

			glfwGetMonitorPos(monitors[i], &rect.x, &rect.y);
			rect.width = mode != nullptr ? mode->width : 0;
			rect.height = mode != nullptr ? mode->height : 0;
			rects.push_back(rect);
		};

		m_maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);

		if (m_maximized)
			glfwRestoreWindow(window);

		glfwGetWindowPos(window, &m_windowed.x, &m_windowed.y);
		glfwGetWindowSize(window, &m_windowed.width, &m_windowed.height);

		const int under = MonitorUnder(m_windowed, rects);
		const GLFWvidmode* mode = under >= 0 ? glfwGetVideoMode(monitors[under]) : nullptr;

		if (mode == nullptr)
		{
			if (m_maximized)
				glfwMaximizeWindow(window);

			return;
		};

		// A click on another monitor would otherwise minimise it.
		glfwSetWindowAttrib(window, GLFW_AUTO_ICONIFY, GLFW_FALSE);
		glfwSetWindowMonitor(window, monitors[under], 0, 0, mode->width, mode->height, mode->refreshRate);
		m_moved = true;
	};

	void WindowFullscreen::Leave(GLFWwindow* window)
	{
		if (!m_on)
			return;

		m_on = false;

		if (window == nullptr || !m_moved)
			return;

		glfwSetWindowMonitor(window, nullptr, m_windowed.x, m_windowed.y, m_windowed.width, m_windowed.height, GLFW_DONT_CARE);

		if (m_maximized)
			glfwMaximizeWindow(window);
	};
};
