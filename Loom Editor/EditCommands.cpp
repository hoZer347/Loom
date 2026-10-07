#include "EditCommands.h"

#include "GameObject.h"
#include "Scene.h"

#include "imgui.h"


namespace Loom
{
	EditCommand EditCommands::FromShortcut(bool hierarchy_focused)
	{
		if (ImGui::GetIO().WantTextInput)
			return EditCommand::None;

		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
			return EditCommand::Undo;

		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
			ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
			return EditCommand::Redo;

		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_X))
			return EditCommand::Cut;

		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
			return EditCommand::Copy;

		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V))
			return EditCommand::Paste;

		if (hierarchy_focused && ImGui::IsKeyChordPressed(ImGuiKey_Delete))
			return EditCommand::Delete;

		return EditCommand::None;
	};

	bool EditCommands::CanTake(const GameObject* selected)
	{
		return selected != nullptr && selected->GetParent() != nullptr;
	};

	GameObject* EditCommands::PasteParent(GameObject* selected, Scene* active)
	{
		if (CanTake(selected))
			return selected->GetParent();

		if (selected)
			return selected;

		return active
			? &active->GetRoot()
			: nullptr;
	};
};
