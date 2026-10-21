#include "FolderTree.h"

#include "EditorIcons.h"

#include <cfloat>
#include <cstdio>
#include <iostream>
#include <utility>


namespace Loom
{
	void FolderTree::Draw(VirtualFolders& folders, const VirtualFolders::Node& node, const Hooks& hooks)
	{
		const bool renaming = node.path == m_renaming;

		if ((hooks.reveal && hooks.reveal(node.path)) ||
			(m_focus && VirtualFolders::IsAtOrUnder(VirtualFolders::ParentOf(m_renaming), node.path)))
			ImGui::SetNextItemOpen(true);

		if (hooks.row)
			hooks.row();

		const bool open = ImGui::TreeNodeEx(
			("folder:" + node.path).c_str(),
			hooks.span | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick,
			"%s %s",
			EditorIcons::Folder,
			renaming ? "" : node.name.c_str());

		if (!renaming && ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(hooks.folder_payload, node.path.c_str(), node.path.size() + 1);
			ImGui::Text("%s %s", EditorIcons::Folder, node.name.c_str());
			ImGui::EndDragDropSource();
		};

		if (ImGui::BeginDragDropTarget())
		{
			Drop(folders, node.path, hooks);
			ImGui::EndDragDropTarget();
		};

		if (ImGui::BeginPopupContextItem())
		{
			if (hooks.create)
			{
				hooks.create(node.path);
				ImGui::Separator();
			};

			if (ImGui::MenuItem("Rename"))
				Rename(node.path);

			// What is in it goes up a level rather than with it.
			if (ImGui::MenuItem("Remove Folder"))
			{
				folders.RemoveFolder(node.path);
				hooks.changed();
			};

			ImGui::EndPopup();
		};

		if (renaming)
			DrawRename(folders, hooks);

		if (!open)
			return;

		for (const VirtualFolders::Node& child : node.children)
			Draw(folders, child, hooks);

		for (const std::string& item : node.items)
			hooks.item(item);

		ImGui::TreePop();
	};

	void FolderTree::Drop(VirtualFolders& folders, const std::string& folder, const Hooks& hooks)
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(hooks.item_payload))
		{
			folders.Move((const char*)payload->Data, folder);
			hooks.changed();
		};

		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(hooks.folder_payload);

		if (payload == nullptr)
			return;

		const std::string dragged = (const char*)payload->Data;

		// Let go where it was picked up.
		if (dragged == folder)
			return;

		if (folders.MoveFolder(dragged, folder))
			hooks.changed();
		else
			std::cerr << hooks.panel << ": " << dragged << " cannot go inside itself, or beside a folder with its name" << std::endl;
	};

	void FolderTree::Rename(const std::string& folder)
	{
		m_renaming = folder;
		snprintf(m_name, sizeof(m_name), "%s", VirtualFolders::NameOf(folder).c_str());
		m_focus = true;
	};

	void FolderTree::DrawRename(VirtualFolders& folders, const Hooks& hooks)
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);

		if (m_focus)
		{
			ImGui::SetKeyboardFocusHere();
			m_focus = false;
		};

		ImGui::InputText("##rename_folder", m_name, sizeof(m_name), ImGuiInputTextFlags_AutoSelectAll);

		// Enter and clicking away both keep the new name; Escape has already put
		// the old one back by the time the field lets go.
		if (!ImGui::IsItemDeactivated())
			return;

		const std::string folder = std::exchange(m_renaming, "");

		if (folders.RenameFolder(folder, m_name).empty())
			std::cerr << hooks.panel << ": '" << m_name << "' is not a name, or there is already a folder called that there" << std::endl;
		else
			hooks.changed();
	};
};
