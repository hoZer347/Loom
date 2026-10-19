#pragma once


namespace Loom
{
	/**
	* Loom::GameFocus
	* - Whether the game hears the mouse this frame, from what ImGui says of
	*   the Game panel
	* - ImGui moves focus only at the end of the frame a left click lands in,
	*   and never for the other buttons, so a press belongs to the panel it
	*   lands on until every button is up again
	*/
	struct GameFocus final
	{
		// Once a frame. The panel's two are false while it is not drawn, and a
		// button is held through the frame it comes up in, so the press's
		// panel hears the release too.
		bool Update(bool panel_focused, bool panel_hovered, bool button_went_down, bool button_held);

	private:
		bool m_pressInPanel = false;
	};
};
