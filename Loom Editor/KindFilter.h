#pragma once

#include "imgui.h"

#include <functional>
#include <set>
#include <span>
#include <string>
#include <string_view>


namespace Loom
{
	/**
	* Loom::KindFilter
	* - The row of icons along the bottom of a panel that lists files, as the
	*   Project, Scripts and 2D panels do, one for each kind of file the panel
	*   lists. Each is highlighted while its kind shows and dimmed while it is
	*   hidden, a click toggles it, and its tooltip names its kind
	* - A kind is known by its icon, the one the panel shows beside its files
	*/
	struct KindFilter final
	{
		struct Kind
		{
			const char* icon;
			const char* name;
		};

		// How tall the row is in the width left in the window, which it wraps
		// onto as many lines as it needs.
		static float Height(std::span<const Kind> kinds);

		// The row, along the bottom of the space left in the window, drawn
		// before the list it filters so that a click shows in the list on the
		// same frame. The cursor stays where it was, and what comes back is
		// the size of the list above the row.
		ImVec2 DrawUnder(std::span<const Kind> kinds);

		// Whether the files beside this icon show. They do until hidden.
		bool Shows(std::string_view icon) const;
		void Show(std::string_view icon, bool shown);

	private:
		void Draw(std::span<const Kind> kinds);

		std::set<std::string, std::less<>> m_hidden{ };
	};
};
