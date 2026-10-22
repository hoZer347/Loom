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

		bool Searching() const { return m_text[0] != '\0'; };

		// Always true while nothing is typed.
		bool Matches(std::string_view name) const;

	private:
		static constexpr size_t text_size = 128;

		char m_text[text_size]{ };
	};
};
