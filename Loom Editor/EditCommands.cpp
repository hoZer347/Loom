#include "EditCommands.h"

#include "GameObject.h"
#include "Scene.h"

#include "imgui.h"

#include <algorithm>


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

	bool EditCommands::CanMove(const GameObject& dragged, const GameObject& target, const std::vector<Scene*>& owned)
	{
		for (const GameObject* above = &target; above != nullptr; above = above->GetParent())
			if (above == &dragged)
				return false;

		Scene* from = SceneOf(dragged);
		Scene* to = SceneOf(target);

		if (from == nullptr || to == nullptr)
			return false;

		if (from == to)
			return true;

		const auto is_owned = [&](Scene* scene) { return std::find(owned.begin(), owned.end(), scene) != owned.end(); };

		return is_owned(from) && is_owned(to) && from->GetGuid() != to->GetGuid();
	};

	Scene* EditCommands::SceneOf(const GameObject& gameObject)
	{
		const GameObject* root = &gameObject;

		while (root->GetParent() != nullptr)
			root = root->GetParent();

		for (Scene* scene : Scene::GetScenes())
			if (&scene->GetRoot() == root)
				return scene;

		return nullptr;
	};
};
