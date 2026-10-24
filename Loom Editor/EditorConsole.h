#pragma once

#include "EditorLog.h"

#include <compare>
#include <cstddef>
#include <functional>
#include <optional>


namespace Loom
{
	/**
	* Loom::EditorConsole
	* - The Console panel: EditorLog's lines, a filter, and a mouse selection
	*   that copies out with Ctrl+C or the right-click menu
	* - A file a line links to is followed by a click on it
	*/
	struct EditorConsole final
	{
		void Draw(bool* open, const std::function<void(const EditorLog::Link&)>& follow);

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

		// The line and start of the link the mouse went down on, followed if
		// it comes up there without having selected anything.
		std::optional<Position> m_pressedLink{ };
	};
};
