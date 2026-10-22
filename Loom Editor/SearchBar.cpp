#include "SearchBar.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cfloat>


namespace Loom
{
	void SearchBar::Draw()
	{
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##search", "Search", m_text, sizeof(m_text));
	};

	bool SearchBar::Matches(std::string_view name) const
	{
		const std::string_view text = m_text;

		return text.empty() || !std::ranges::search(
			name,
			text,
			[](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); }).empty();
	};
};
