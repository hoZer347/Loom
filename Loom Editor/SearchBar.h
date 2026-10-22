#pragma once

#include <cstddef>
#include <string_view>


namespace Loom
{
	/**
	* Loom::SearchBar
	* - A field across the top of a panel's list, which narrows the list to
	*   what has the typed text in its name, whatever its case
	* - FolderTree has one, so every panel that files things in one can be
	*   searched
	*/
	struct SearchBar final
	{
		void Draw();
		void Clear() { m_text[0] = '\0'; };

		bool Searching() const { return m_text[0] != '\0'; };

		// Always true while nothing is typed.
		bool Matches(std::string_view name) const;

		// Around the list it narrows. While searching, the folders' rows are
		// kept apart from their own, so the search's opening and closing them
		// leaves them as they were once it is cleared.
		void BeginResults();
		void EndResults();

		// Before a folder's row, which opens to show what a search finds each
		// time its text changes.
		void OpenFound() const;

	private:
		static constexpr size_t text_size = 128;

		char m_text[text_size]{ };
		bool m_changed = false;
		bool m_scoped = false;
	};
};
