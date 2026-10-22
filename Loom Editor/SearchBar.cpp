#include "SearchBar.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <utility>


namespace Loom
{
	void SearchBar::Draw()
	{
		ImGui::SetNextItemWidth(-FLT_MIN);
		m_changed = ImGui::InputTextWithHint("##search", "Search", m_text, sizeof(m_text));
	};

	void SearchBar::BeginResults()
	{
		m_scoped = Searching();

		if (m_scoped)
			ImGui::PushID("##found");
	};

	// Whether it pushed, as something in the list may clear the search.
	void SearchBar::EndResults()
	{
		if (std::exchange(m_scoped, false))
			ImGui::PopID();
	};

	void SearchBar::OpenFound() const
	{
		if (m_changed && Searching())
			ImGui::SetNextItemOpen(true);
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
