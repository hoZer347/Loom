#pragma once

#include "VirtualFolders.h"

#include "imgui.h"

#include <cstddef>
#include <functional>
#include <string>


namespace Loom
{
	/**
	* Loom::FolderTree
	* - A panel's VirtualFolders drawn as a tree, as the Scripts and Key
	*   Bindings panels show theirs: a row a folder, opened by its arrow or a
	*   double click, dragged into another, renamed in place and taken out
	*   from its right-click menu, with the panel's items inside
	* - The panel draws its items and the top of each folder's menu, and
	*   hears when the folders change, through Hooks
	*/
	struct FolderTree final
	{
		struct Hooks
		{
			// What a dragged item and a dragged folder carry: its name or path.
			const char* item_payload = nullptr;
			const char* folder_payload = nullptr;

			// The panel's name, for what the console is told.
			const char* panel = nullptr;

			// How a folder's row spans: the list's width, or a table's columns.
			ImGuiTreeNodeFlags span = ImGuiTreeNodeFlags_SpanAvailWidth;

			// Before each folder's row, as a table starts one.
			std::function<void()> row{ };

			// Whether a folder must be open, to show something in it.
			std::function<bool(const std::string& folder)> reveal{ };

			// The top of a folder's right-click menu, which makes things in it.
			std::function<void(const std::string& folder)> create{ };

			std::function<void(const std::string& item)> item{ };

			// After the folders, or what is filed in them, change.
			std::function<void()> changed{ };
		};

		// The folder's row and, while it is open, the folders and items in it.
		void Draw(VirtualFolders& folders, const VirtualFolders::Node& node, const Hooks& hooks);

		// Takes an item or a folder dropped on the target begun last into folder.
		static void Drop(VirtualFolders& folders, const std::string& folder, const Hooks& hooks);

		// Has the folder's name typed in place from the next frame, the folders
		// it is in opened to show it.
		void Rename(const std::string& folder);

	private:
		static constexpr size_t name_size = 128;

		void DrawRename(VirtualFolders& folders, const Hooks& hooks);

		std::string m_renaming{ };
		char m_name[name_size]{ };
		bool m_focus = false;
	};
};
