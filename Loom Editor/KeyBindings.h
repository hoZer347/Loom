#pragma once

#include "InputActions.h"
#include "VirtualFolders.h"

#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::KeyBindings
	* - The Key Bindings panel: the project's InputActions, each a name and two
	*   keys picked from a list, added, renamed and taken out here, and filed
	*   in folders as the Scripts panel files scripts, by dragging or from the
	*   right-click menu
	* - A name is taken when its box is let go of, and only when
	*   InputActions::Tidy keeps it and no other action has it
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
			std::vector<std::string> removed;
			bool changed = false;

			// Whether the folders changed, so each action's comes from them.
			bool refiled = false;
		};

		bool DrawActions();
		void DrawFolder(const VirtualFolders::Node& node, Edit& edit);
		void DrawAction(InputActions::Action& action, Edit& edit);
		void DrawFolderRename(Edit& edit);

		// Add Action and Add Folder into folder, as buttons or menu items.
		void DrawAddItems(const std::string& folder, Edit& edit, bool menu);

		// Takes an action or a folder dropped on the target begun last, into folder.
		static void Drop(const std::string& folder, Edit& edit);

		// Whether key was set to another.
		static bool DrawKey(const char* label, Key& key);

		// "Action", or the first "Action 2", "Action 3" and on no action has.
		static std::string FreeName(const std::vector<InputActions::Action>& actions);

		// The action whose name is being typed, and what it reads so far.
		std::string m_renaming{ };
		std::string m_name{ };

		// The folder whose name is being typed, what it reads so far, and
		// whether the box wants the keyboard next frame.
		std::string m_renamingFolder{ };
		std::string m_folderName{ };
		bool m_focusFolderName = false;
	};
};
