#include "AttributeGui.h"

#include "AttributeFields.h"

#include "imgui.h"


namespace Loom
{
	void AttributeGui::Draw(const char* name, AttributeBase& attribute)
	{
		ImGui::PushID(&attribute);

		double baseValue = 0.0;
		double value = 0.0;

		if (attribute.TryGetNumbers(baseValue, value))
		{
			double edited = baseValue;

			if (ImGui::InputDouble(name, &edited, 0.0, 0.0, "%.3f")
				&& edited != baseValue)
				attribute.TrySetBaseNumber(edited);

			const double delta = value - baseValue;

			ImGui::SameLine();

			// The delta is the whole point of a modifier stack: a number on its own says
			// nothing about whether anything is acting on it.
			if (delta > 0.0)
				ImGui::TextColored(ImVec4(0.2f, 0.7f, 0.2f, 1.0f), "= %.3f (+%.3f)", value, delta);
			else if (delta < 0.0)
				ImGui::TextColored(ImVec4(0.8f, 0.3f, 0.3f, 1.0f), "= %.3f (%.3f)", value, delta);
			else ImGui::Text("= %.3f", value);
		}
		// A string attribute has no delta to draw, so it is shown rather than edited.
		else ImGui::Text("%s: %s", name, attribute.GetValueString().c_str());

		const size_t modifiers = attribute.ModifierCount();

		if (modifiers > 0)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("[%zu mod]", modifiers);
		};

		ImGui::PopID();
	};

	void AttributeGui::Draw(AttributeOwner& owner)
	{
		for (const AttributeField& field : owner.Attributes())
			if (field.Attribute != nullptr)
				Draw(field.Name.c_str(), *field.Attribute);
	};
};
