#include "WindowFullscreen.h"

#include "OpenGL.h"

#include <algorithm>
#include <sstream>


namespace Loom
{
	namespace
	{
		std::vector<WindowFullscreen::Rect> MonitorRects(GLFWmonitor** monitors, int count)
		{
			std::vector<WindowFullscreen::Rect> rects;

			for (int i = 0; i < count; i++)
			{
				const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
				WindowFullscreen::Rect rect;

				glfwGetMonitorPos(monitors[i], &rect.x, &rect.y);
				rect.width = mode != nullptr ? mode->width : 0;
				rect.height = mode != nullptr ? mode->height : 0;
				rects.push_back(rect);
			};

			return rects;
		};
	};

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
		const std::vector<Rect> rects = MonitorRects(monitors, count);

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

	void WindowFullscreen::Track(GLFWwindow* window)
	{
		const bool shown = window != nullptr && glfwGetWindowAttrib(window, GLFW_VISIBLE);

		if ((m_on && m_moved) || !shown || glfwGetWindowAttrib(window, GLFW_ICONIFIED))
			return;

		m_maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);

		Rect& rect = m_maximized ? m_zoomed : m_windowed;

		glfwGetWindowPos(window, &rect.x, &rect.y);
		glfwGetWindowSize(window, &rect.width, &rect.height);
	};

	WindowFullscreen::Placement WindowFullscreen::Placed(GLFWwindow* window) const
	{
		Placement placed;

		placed.windowed = m_windowed;
		placed.maximized = m_maximized;
		placed.minimized = window != nullptr && glfwGetWindowAttrib(window, GLFW_ICONIFIED);

		if (m_maximized && !m_on)
		{
			int count = 0;
			GLFWmonitor** monitors = glfwGetMonitors(&count);

			placed.windowed = Carried(m_windowed, m_zoomed, MonitorRects(monitors, count));
		};

		return placed;
	};

	void WindowFullscreen::Place(GLFWwindow* window, const Placement& placement)
	{
		if (window == nullptr || !glfwGetWindowAttrib(window, GLFW_VISIBLE))
			return;

		// The size last: arriving on a monitor of another scale may change it.
		glfwSetWindowPos(window, placement.windowed.x, placement.windowed.y);
		glfwSetWindowSize(window, placement.windowed.width, placement.windowed.height);
	};

	void WindowFullscreen::Adopt(GLFWwindow* window, const Placement& placement)
	{
		m_windowed = placement.windowed;
		m_maximized = placement.maximized;

		if (window == nullptr || !glfwGetWindowAttrib(window, GLFW_VISIBLE))
			return;

		if (m_maximized && !m_on)
			glfwMaximizeWindow(window);

		if (placement.minimized)
			glfwIconifyWindow(window);
	};

	WindowFullscreen::Rect WindowFullscreen::Carried(const Rect& windowed, const Rect& maximized, const std::vector<Rect>& monitors)
	{
		const int from = MonitorUnder(windowed, monitors);
		const int to = MonitorUnder(maximized, monitors);

		if (from == to)
			return windowed;

		Rect carried = windowed;

		carried.x += monitors[to].x - monitors[from].x;
		carried.y += monitors[to].y - monitors[from].y;

		return carried;
	};

	std::string WindowFullscreen::Write(const Placement& placement)
	{
		const Rect& rect = placement.windowed;

		return std::to_string(rect.x) + ',' + std::to_string(rect.y) + ',' +
			std::to_string(rect.width) + ',' + std::to_string(rect.height) + ',' +
			std::to_string((int)placement.maximized) + ',' + std::to_string((int)placement.minimized) + ',' +
			std::to_string(placement.view);
	};

	std::optional<WindowFullscreen::Placement> WindowFullscreen::Read(const std::string& text)
	{
		Placement placement;
		Rect& rect = placement.windowed;
		int maximized = 0;
		int minimized = 0;

		int* const fields[] = { &rect.x, &rect.y, &rect.width, &rect.height, &maximized, &minimized, &placement.view };
		std::istringstream stream(text);

		for (int* field : fields)
		{
			if (field != fields[0] && stream.get() != ',')
				return std::nullopt;

			if (!(stream >> *field))
				return std::nullopt;
		};

		if (stream.peek() != std::char_traits<char>::eof() || rect.width <= 0 || rect.height <= 0 || placement.view < no_view)
			return std::nullopt;

		placement.maximized = maximized != 0;
		placement.minimized = minimized != 0;

		return placement;
	};
};
