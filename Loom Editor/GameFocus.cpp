#include "GameFocus.h"


namespace Loom
{
	bool GameFocus::Update(bool panel_focused, bool panel_hovered, bool button_went_down, bool button_held)
	{
		if (button_went_down)
			m_pressInPanel = panel_hovered;

		return button_held ? m_pressInPanel : panel_focused;
	};
};
