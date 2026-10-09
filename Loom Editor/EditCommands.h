#pragma once

#include <vector>


namespace Loom
{
	struct GameObject;
	struct Scene;

	enum class EditCommand
	{
		None,
		Undo,
		Redo,
		Cut,
		Copy,
		Paste,
		Delete,
	};

	namespace EditCommands
	{
		// The edit the keys pressed this ImGui frame ask for. Nothing while a
		// text box has the keyboard, and Delete only while the Hierarchy has
		// focus, since everywhere else it means something else or nothing.
		EditCommand FromShortcut(bool hierarchy_focused);

		// A scene's root is not the user's to cut, copy or delete.
		bool CanTake(const GameObject* selected);

		// Where a paste lands: beside the selection, under it when it is a
		// scene's root, and under the active scene's root when nothing is
		// selected. Null when there is nowhere.
		GameObject* PasteParent(GameObject* selected, Scene* active);

		// Whether a Hierarchy drag may put dragged under target's parent or
		// under target. Never under itself. From one scene into another only
		// when both are owned, since only those are saved and undone, and
		// never between two copies of one scene, which are kept identical.
		bool CanMove(const GameObject& dragged, const GameObject& target, const std::vector<Scene*>& owned);

		// The scene whose root the object is under, or null if none.
		Scene* SceneOf(const GameObject& gameObject);
	};
};
