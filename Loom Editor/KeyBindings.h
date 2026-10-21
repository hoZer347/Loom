#pragma once

#include "FolderTree.h"
#include "InputActions.h"
#include "VirtualFolders.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::KeyBindings
	* - The Key Bindings panel: the project's InputActions, each a name and the
	*   input that does it, a key or keys held together picked from a list,
	*   added, renamed and taken out here, and filed in folders as the Scripts
	*   panel files scripts, by dragging or from the right-click menu
	* - Another way to do an action is another row by the same name
	* - A name is taken when its box is let go of, and only when
	*   InputActions::Tidy keeps it
	*/
	struct KeyBindings final
	{
		// Whether the actions changed, for the editor to save them. Without a
		// project open there is nowhere to save them, so none are shown.
		bool Draw(bool* open, bool project_open);

	private:
		// The actions and their folders as this frame changes them.
		struct Edit
		{
			std::vector<InputActions::Action> actions;
			VirtualFolders folders;

			// Where in actions those to go are.
			std::vector<size_t> removed;

			float key_width = 0.0f;
			bool changed = false;

			// Whether the folders changed, so each action's comes from them.
			bool refiled = false;
		};

		bool DrawActions();

		// How the FolderTree draws and files this frame's actions.
		FolderTree::Hooks Hooks(Edit& edit);

		void DrawAction(size_t index, Edit& edit, const FolderTree::Hooks& hooks);

		// Add Action and Add Folder into folder, as buttons or menu items.
		void DrawAddItems(const std::string& folder, Edit& edit, bool menu);

		// A list for each key, joined by +, and a + that adds one. Whether the
		// keys changed.
		static bool DrawInput(std::vector<Key>& keys, float key_width);

		// Whether key was set to another.
		static bool DrawKey(const char* label, Key& key);

		// "Action", or the first "Action 2", "Action 3" and on no action has.
		static std::string FreeName(const std::vector<InputActions::Action>& actions);

		// The action whose name is being typed, by where it is in the list, and
		// what it reads so far.
		std::optional<size_t> m_renaming{ };
		std::string m_name{ };

		FolderTree m_tree;
	};
};
