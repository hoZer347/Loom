#pragma once

#include <vector>

struct GLFWwindow;


namespace Loom
{
	/**
	* Loom::WindowFullscreen
	* - Puts a GLFW window over the whole of the monitor it is mostly on, at
	*   that monitor's own mode, and back where it was, as large and as
	*   maximized as it was
	* - A window that is not shown is left alone, so a hidden run never takes
	*   anyone's screen; it still counts as on, for whatever the caller lays
	*   out inside it
	*/
	struct WindowFullscreen final
	{
		struct Rect
		{
			int x = 0;
			int y = 0;
			int width = 0;
			int height = 0;
		};

		void Enter(GLFWwindow* window);
		void Leave(GLFWwindow* window);

		bool IsOn() const { return m_on; };

		// Which of monitors window overlaps most, or the first when it
		// overlaps none; -1 when there are none.
		static int MonitorUnder(const Rect& window, const std::vector<Rect>& monitors);

	private:
		bool m_on = false;
		bool m_moved = false;
		bool m_maximized = false;
		Rect m_windowed{ };
	};
};
