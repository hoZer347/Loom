#include "FolderTree.h"

#include "EditorIcons.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <iostream>
#include <utility>


namespace Loom
{
	void FolderTree::Draw(VirtualFolders& folders, const VirtualFolders::Node& top, const Hooks& hooks)
	{
		m_search.BeginResults();

		for (const VirtualFolders::Node& folder : top.children)
			if (Shows(folder, hooks))
				DrawFolder(folders, folder, hooks, false);

		for (const std::string& item : top.items)
			if (Shows(item, hooks))
				hooks.item(item);

		m_search.EndResults();
	};

	void FolderTree::DrawFolder(VirtualFolders& folders, const VirtualFolders::Node& node, const Hooks& hooks, bool found)
	{
		const bool renaming = node.path == m_renaming;

		m_search.OpenFound();

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

		if (hooks.arrange && !renaming && ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(hooks.folder_payload, node.path.c_str(), node.path.size() + 1);
			ImGui::Text("%s %s", EditorIcons::Folder, node.name.c_str());
			ImGui::EndDragDropSource();
		};

		if (hooks.arrange && ImGui::BeginDragDropTarget())
		{
			Drop(folders, node.path, hooks);
			ImGui::EndDragDropTarget();
		};

		if (hooks.arrange && ImGui::BeginPopupContextItem())
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

		found = found || m_search.Matches(node.name);

		for (const VirtualFolders::Node& child : node.children)
			if (found || Shows(child, hooks))
				DrawFolder(folders, child, hooks, found);

		for (const std::string& item : node.items)
			if (found || Shows(item, hooks))
				hooks.item(item);

		ImGui::TreePop();
	};

	bool FolderTree::Shows(const VirtualFolders::Node& node, const Hooks& hooks) const
	{
		return m_search.Matches(node.name) ||
			std::ranges::any_of(node.items, [&](const std::string& item) { return Shows(item, hooks); }) ||
			std::ranges::any_of(node.children, [&](const VirtualFolders::Node& child) { return Shows(child, hooks); });
	};

	bool FolderTree::Shows(const std::string& item, const Hooks& hooks) const
	{
		return m_search.Matches(hooks.name ? hooks.name(item) : item);
	};

	void FolderTree::Drop(VirtualFolders& folders, const std::string& folder, const Hooks& hooks)
	{
		const ImGuiPayload* held = ImGui::GetDragDropPayload();

		if (held != nullptr &&
			held->IsDataType(hooks.item_payload) &&
			(!hooks.takes || hooks.takes((const char*)held->Data)) &&
			ImGui::AcceptDragDropPayload(hooks.item_payload))
		{
			folders.Move((const char*)held->Data, folder);
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
		// The folder may not match the search under either name.
		m_search.Clear();
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
