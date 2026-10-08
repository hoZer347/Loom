#pragma once

#include <compare>
#include <cstddef>


namespace Loom
{
	/**
	* Loom::EditorConsole
	* - The Console panel: EditorLog's lines, a filter, and a mouse selection
	*   that copies out with Ctrl+C or the right-click menu
	*/
	struct EditorConsole final
	{
		void Draw(bool* open);

	private:
		// A point in the text: an EditorLog line id and a byte offset into it.
		struct Position
		{
			size_t line = 0;
			size_t offset = 0;

			auto operator<=>(const Position&) const = default;
		};

		bool m_autoScroll = true;
		char m_filter[128]{ };

		// The selection runs between these two, and is empty when they match.
		Position m_anchor{ };
		Position m_caret{ };
		bool m_dragging = false;
		bool m_copy = false;
	};
};
