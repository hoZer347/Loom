#pragma once

#include "EditHistory.h"
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
		#pragma region Panel

		// Whether Ctrl+S asked for the actions to be saved. Changes are held
		// in InputActions until then, and Ctrl+Z and Ctrl+Y step through
		// them. Without a project open there is nowhere to save them, so none
		// are shown.
		bool Draw(bool* open, bool project_open);

		bool IsFocused() const { return m_focused; };

		#pragma endregion

		#pragma region Saving

		// InputActions as they are now are as their file has them, with
		// nothing to undo.
		void Loaded();

		// InputActions as they are now were written to their file.
		void Saved();

		bool Unsaved() const;

		#pragma endregion

	private:
		#pragma region Panel

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

		// Ctrl+Z and Ctrl+Y, and whether Ctrl+S was pressed.
		bool Shortcuts();

		static InputActions::File Current();

		// How the FolderTree draws and files this frame's actions.
		FolderTree::Hooks Hooks(Edit& edit);

		// Add Action and Add Folder into folder, as buttons or menu items.
		void DrawAddItems(const std::string& folder, Edit& edit, bool menu);

		// "Action", or the first "Action 2", "Action 3" and on no action has.
		static std::string FreeName(const std::vector<InputActions::Action>& actions);

		#pragma endregion

		#pragma region Rows

		void DrawAction(size_t index, Edit& edit, const FolderTree::Hooks& hooks);

		// A list for each key, joined by +, and a + that adds one. Whether the
		// keys changed.
		static bool DrawInput(std::vector<Key>& keys, float key_width);

		// The key picked from the list this frame, the one key is included, or none.
		static std::optional<Key> DrawKey(const char* label, Key key);

		#pragma endregion

		#pragma region State

		// The action whose name is being typed, by where it is in the list, and
		// what it reads so far.
		std::optional<size_t> m_renaming{ };
		std::string m_name{ };

		FolderTree m_tree;

		EditHistory<InputActions::File> m_history;
		bool m_focused = false;

		#pragma endregion
	};
};
