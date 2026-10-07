#include "AxisGui.h"

#include "imgui.h"
#include "imgui_internal.h"


namespace Loom
{
	namespace
	{
		constexpr ImU32 axis_colors[] =
		{
			IM_COL32(219, 64, 64, 255),
			IM_COL32(92, 189, 64, 255),
			IM_COL32(64, 115, 230, 255),
		};

		constexpr int axis_count = IM_ARRAYSIZE(axis_colors);

		constexpr float stripe_width = 3.0f;
	};

	// ImGui::DragScalarN, with the stripe drawn over each box after it.
	bool AxisGui::DragFloatN(const char* label, float* values, int count, float speed, const char* format)
	{
		if (ImGui::GetCurrentWindow()->SkipItems)
			return false;

		const ImGuiStyle& style = ImGui::GetStyle();
		bool changed = false;

		ImGui::BeginGroup();
		ImGui::PushID(label);
		ImGui::PushMultiItemsWidths(count, ImGui::CalcItemWidth());

		for (int i = 0; i < count; i++)
		{
			ImGui::PushID(i);

			if (i > 0)
				ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);

			changed |= ImGui::DragFloat("", &values[i], speed, 0.0f, 0.0f, format);

			if (i < axis_count)
			{
				const ImVec2 min = ImGui::GetItemRectMin();
				const ImVec2 max = ImGui::GetItemRectMax();

				ImGui::GetWindowDrawList()->AddRectFilled(
					min,
					ImVec2(min.x + stripe_width, max.y),
					axis_colors[i],
					style.FrameRounding,
					ImDrawFlags_RoundCornersLeft);
			};

			ImGui::PopID();
			ImGui::PopItemWidth();
		};

		ImGui::PopID();

		const char* label_end = ImGui::FindRenderedTextEnd(label);

		if (label != label_end)
		{
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			ImGui::TextEx(label, label_end);
		};

		ImGui::EndGroup();

		return changed;
	};
};
