#pragma once

#include <optional>
#include <string>
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
	* - Knows the Placement of its window, so a restart comes back as it was,
	*   down to where leaving fullscreen or maximized lands
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

		// The view Placement holds when it is not fullscreen.
		static constexpr int no_view = -1;

		// A window's state, for the next process to take up. windowed is
		// where it sits when neither maximized nor fullscreen.
		struct Placement
		{
			Rect windowed{ };
			bool maximized = false;
			bool minimized = false;
			int view = no_view;
		};

		void Enter(GLFWwindow* window);
		void Leave(GLFWwindow* window);

		// Called once a frame, since a maximized or minimized window no longer
		// says where it sits otherwise.
		void Track(GLFWwindow* window);

		Placement Placed(GLFWwindow* window) const;

		// Puts a new window where placement has it, as soon as it opens.
		static void Place(GLFWwindow* window, const Placement& placement);

		// Takes over from a Place on window, once it has its frame and any
		// Enter is done.
		void Adopt(GLFWwindow* window, const Placement& placement);

		bool IsOn() const { return m_on; };

		// Which of monitors window overlaps most, or the first when it
		// overlaps none; -1 when there are none.
		static int MonitorUnder(const Rect& window, const std::vector<Rect>& monitors);

		// Where windowed lands if a window maximized at maximized is restored,
		// which moves it along when that window was sent to another monitor.
		static Rect Carried(const Rect& windowed, const Rect& maximized, const std::vector<Rect>& monitors);

		// As an argument of --window-placement. Read has no value for anything
		// Write would not give.
		static std::string Write(const Placement& placement);
		static std::optional<Placement> Read(const std::string& text);

	private:
		bool m_on = false;
		bool m_moved = false;
		bool m_maximized = false;
		Rect m_windowed{ };
		Rect m_zoomed{ };
	};
};
