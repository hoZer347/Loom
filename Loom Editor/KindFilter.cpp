#include "KindFilter.h"

#include "imgui.h"
#include "imgui_internal.h"


namespace Loom
{
	namespace
	{
		// A frame high, since the icons stand taller than a line of text.
		ImVec2 SizeOf(const KindFilter::Kind& kind)
		{
			return ImVec2(
				ImGui::CalcTextSize(kind.icon).x + ImGui::GetStyle().FramePadding.x * 2.0f,
				ImGui::GetFrameHeight());
		};

		// Moves x, how far along its line the row has come, past an icon
		// width wide. Whether the icon starts a line, as it does when the line
		// has no room left for it.
		bool StartsLine(float& x, float width, float available)
		{
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const bool starts = x == 0.0f || x + spacing + width > available;

			x = starts ? width : x + spacing + width;

			return starts;
		};
	};

	float KindFilter::Height(std::span<const Kind> kinds)
	{
		const float available = ImGui::GetContentRegionAvail().x;

		float x = 0.0f;
		int lines = 0;

		for (const Kind& kind : kinds)
			if (StartsLine(x, SizeOf(kind).x, available))
				lines++;

		return lines == 0
			? 0.0f
			: lines * ImGui::GetFrameHeight() + (lines - 1) * ImGui::GetStyle().ItemSpacing.y;
	};

	ImVec2 KindFilter::DrawUnder(std::span<const Kind> kinds)
	{
		const float height = Height(kinds);
		const ImVec2 cursor = ImGui::GetCursorPos();

		ImGui::SetCursorPosY(cursor.y + ImGui::GetContentRegionAvail().y - height);
		Draw(kinds);
		ImGui::SetCursorPos(cursor);

		return ImVec2(0.0f, -(height + ImGui::GetStyle().ItemSpacing.y));
	};

	void KindFilter::Draw(std::span<const Kind> kinds)
	{
		const float available = ImGui::GetContentRegionAvail().x;

		float x = 0.0f;

		ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));

		for (const Kind& kind : kinds)
		{
			const ImVec2 size = SizeOf(kind);
			const bool shown = Shows(kind.icon);

			if (!StartsLine(x, size.x, available))
				ImGui::SameLine();

			if (!shown)
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

			// Unpadded, so the spacing between them shows, and toggled as the
			// mouse goes down rather than up.
			if (ImGui::Selectable(
				(std::string(kind.icon) + "##" + kind.name).c_str(),
				shown,
				ImGuiSelectableFlags_NoPadWithHalfSpacing | ImGuiSelectableFlags_SelectOnClick,
				size))
				Show(kind.icon, !shown);

			if (!shown)
				ImGui::PopStyleColor();

			ImGui::SetItemTooltip("%s", kind.name);
		};

		ImGui::PopStyleVar();
	};

	bool KindFilter::Shows(std::string_view icon) const
	{
		return !m_hidden.contains(icon);
	};

	void KindFilter::Show(std::string_view icon, bool shown)
	{
		if (shown)
		{
			if (const auto found = m_hidden.find(icon); found != m_hidden.end())
				m_hidden.erase(found);
		}
		else
			m_hidden.emplace(icon);
	};
};
