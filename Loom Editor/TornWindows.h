#pragma once

#include "imgui.h"

#include <functional>
#include <string>
#include <vector>

struct ImGuiWindow;


namespace Loom
{
	/**
	* Loom::TornWindows
	* - A panel let go outside the editor docks into an OS window of its own,
	*   which has a menu bar and a dock space as the editor's own window does
	* - Panels docked in one together are torn off together
	* - The window goes once no panel is left in it
	*/
	struct TornWindows final
	{
		struct Torn
		{
			std::string name;
			ImGuiID dockspace = 0;
			ImVec2 pos{ };
			ImVec2 size{ };
		};

		using Bar = std::function<void(const Torn&)>;

		// Before the panels: docks those let go outside the editor into new
		// windows, then begins each window with bar inside its menu bar, which
		// is laid out with bar_padding as its frame padding.
		void Draw(ImVec2 bar_padding, const Bar& bar);

		// After the panels: a window with none left in it goes.
		void Prune();

		// The panels docked in torn.
		std::vector<ImGuiWindow*> Docked(const Torn& torn) const;

		const std::vector<Torn>& Windows() const { return m_torn; };

	private:
		void Tear(float bar_height);

		std::vector<Torn> m_torn;
		int m_count = 0;
	};
};
