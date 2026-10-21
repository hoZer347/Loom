#include "KeyBindings.h"

#include "EditorGui.h"
#include "EditorIcons.h"

#include "imgui.h"

#include <cfloat>
#include <vector>


namespace Loom
{
	namespace
	{
		constexpr int columns = 4;
		constexpr const char* default_name = "Action";

		// What the second action given the default name is numbered.
		constexpr int first_number = 2;
	};

	bool KeyBindings::Draw(bool* open, bool project_open)
	{
		bool changed = false;

		if (ImGui::Begin(EditorPanels::KeyBindings, open))
		{
			if (project_open)
				changed = DrawActions();
			else
				ImGui::TextDisabled("Open a project to bind its keys.");
		};

		ImGui::End();

		return changed;
	};

	bool KeyBindings::DrawActions()
	{
		std::vector<InputActions::Action> actions = InputActions::All();
		std::optional<size_t> removed{ };
		bool changed = false;

		if (ImGui::BeginTable("Actions", columns, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame))
		{
			ImGui::TableSetupColumn("Action");
			ImGui::TableSetupColumn("Key");
			ImGui::TableSetupColumn("Alt");
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			for (size_t i = 0; i < actions.size(); i++)
			{
				InputActions::Action& action = actions[i];

				ImGui::PushID((int)i);
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-FLT_MIN);

				std::string shown = action.name;
				InputTextString("##Name", m_renaming == i ? m_name : shown);

				if (ImGui::IsItemActivated())
				{
					m_renaming = i;
					m_name = action.name;
				};

				if (ImGui::IsItemDeactivated() && m_renaming == i)
				{
					const std::string name = InputActions::Tidy(m_name);

					if (!name.empty() && InputActions::Find(name) == nullptr)
					{
						action.name = name;
						changed = true;
					};

					m_renaming.reset();
				};

				ImGui::TableNextColumn();
				changed |= DrawKey("##Key", action.key);

				ImGui::TableNextColumn();
				changed |= DrawKey("##Alt", action.alt);

				ImGui::TableNextColumn();

				if (ImGui::SmallButton("Remove"))
					removed = i;

				ImGui::PopID();
			};

			ImGui::EndTable();
		};

		if (removed)
		{
			actions.erase(actions.begin() + (std::ptrdiff_t)*removed);
			m_renaming.reset();
			changed = true;
		};

		if (ImGui::Button("Add Action"))
		{
			actions.push_back({ FreeName() });
			changed = true;
		};

		if (changed)
			InputActions::Set(std::move(actions));

		return changed;
	};

	bool KeyBindings::DrawKey(const char* label, Key& key)
	{
		bool changed = false;

		ImGui::SetNextItemWidth(-FLT_MIN);

		if (ImGui::BeginCombo(label, NameOf(key).data(), ImGuiComboFlags_HeightLarge))
		{
			for (const Key each : AllKeys())
				if (ImGui::Selectable(NameOf(each).data(), each == key) && each != key)
				{
					key = each;
					changed = true;
				};

			ImGui::EndCombo();
		};

		return changed;
	};

	std::string KeyBindings::FreeName()
	{
		std::string name = default_name;

		for (int number = first_number; InputActions::Find(name) != nullptr; number++)
			name = std::string(default_name) + " " + std::to_string(number);

		return name;
	};
};
