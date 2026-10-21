#include "KeyBindings.h"

#include "EditorGui.h"
#include "EditorIcons.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <utility>


namespace Loom
{
	namespace
	{
		constexpr int columns = 4;
		constexpr const char* default_name = "Action";
		constexpr const char* default_folder = "New Folder";

		// What the second action or folder given the default name is numbered.
		constexpr int first_number = 2;

		constexpr const char* action_payload = "LoomAction";
		constexpr const char* folder_payload = "LoomActionFolder";

		std::string Numbered(const char* name, int number)
		{
			return std::string(name) + " " + std::to_string(number);
		};

		bool HasAction(const std::vector<InputActions::Action>& actions, const std::string& name)
		{
			return std::any_of(actions.begin(), actions.end(), [&name](const InputActions::Action& action) { return action.name == name; });
		};
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
		Edit edit{ InputActions::All() };

		for (const std::string& folder : InputActions::Folders())
			edit.folders.AddFolder(VirtualFolders::ParentOf(folder), VirtualFolders::NameOf(folder));

		std::vector<std::string> names{ };

		for (const InputActions::Action& action : edit.actions)
		{
			edit.folders.Move(action.name, action.folder);
			names.push_back(action.name);
		};

		const VirtualFolders::Node root = edit.folders.Build(names);
		const FolderTree::Hooks hooks = Hooks(edit);

		DrawAddItems("", edit, false);

		if (ImGui::BeginTable("Actions", columns, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame))
		{
			ImGui::TableSetupColumn("Action");
			ImGui::TableSetupColumn("Key");
			ImGui::TableSetupColumn("Alt");
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			for (const VirtualFolders::Node& folder : root.children)
				m_tree.Draw(edit.folders, folder, hooks);

			for (const std::string& name : root.items)
				hooks.item(name);

			ImGui::EndTable();
		};

		// Dropped anywhere else in the panel, it goes to the top.
		if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect, ImGui::GetID("##top")))
		{
			FolderTree::Drop(edit.folders, "", hooks);
			ImGui::EndDragDropTarget();
		};

		if (ImGui::BeginPopupContextWindow("##add", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			DrawAddItems("", edit, true);
			ImGui::EndPopup();
		};

		if (!edit.changed && !edit.refiled)
			return false;

		std::erase_if(edit.actions, [&edit](const InputActions::Action& action) { return std::ranges::find(edit.removed, action.name) != edit.removed.end(); });

		std::vector<std::string> folders{ };

		if (edit.refiled)
		{
			for (InputActions::Action& action : edit.actions)
				action.folder = edit.folders.FolderOf(action.name);

			folders.assign(edit.folders.Folders().begin(), edit.folders.Folders().end());
		}
		else
			folders = InputActions::Folders();

		InputActions::Set(std::move(edit.actions), std::move(folders));

		return true;
	};

	FolderTree::Hooks KeyBindings::Hooks(Edit& edit)
	{
		FolderTree::Hooks hooks;
		hooks.item_payload = action_payload;
		hooks.folder_payload = folder_payload;
		hooks.panel = "Key Bindings";
		hooks.span = ImGuiTreeNodeFlags_SpanAllColumns;
		hooks.row = []() { ImGui::TableNextRow(); ImGui::TableNextColumn(); };
		hooks.create = [this, &edit](const std::string& folder) { DrawAddItems(folder, edit, true); };
		hooks.changed = [&edit]() { edit.refiled = true; };

		hooks.item = [this, &edit, hooks](const std::string& name)
		{
			for (InputActions::Action& action : edit.actions)
				if (action.name == name)
					DrawAction(action, edit, hooks);
		};

		return hooks;
	};

	void KeyBindings::DrawAction(InputActions::Action& action, Edit& edit, const FolderTree::Hooks& hooks)
	{
		ImGui::PushID(action.name.c_str());
		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		// The handle it is dragged and right-clicked by.
		ImGui::TreeNodeEx(
			(std::string(EditorIcons::Bindings) + "##action").c_str(),
			ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);

		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(action_payload, action.name.c_str(), action.name.size() + 1);
			ImGui::Text("%s %s", EditorIcons::Bindings, action.name.c_str());
			ImGui::EndDragDropSource();
		};

		if (ImGui::BeginDragDropTarget())
		{
			FolderTree::Drop(edit.folders, edit.folders.FolderOf(action.name), hooks);
			ImGui::EndDragDropTarget();
		};

		if (ImGui::BeginPopupContextItem())
		{
			if (!edit.folders.FolderOf(action.name).empty() && ImGui::MenuItem("Move to Top"))
			{
				edit.folders.Move(action.name, "");
				edit.refiled = true;
			};

			if (ImGui::MenuItem("Remove"))
			{
				edit.removed.push_back(action.name);
				edit.changed = true;
			};

			ImGui::EndPopup();
		};

		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);

		std::string shown = action.name;
		InputTextString("##Name", m_renaming == action.name ? m_name : shown);

		if (ImGui::IsItemActivated())
		{
			m_renaming = action.name;
			m_name = action.name;
		};

		if (ImGui::IsItemDeactivated() && m_renaming == action.name)
		{
			const std::string name = InputActions::Tidy(m_name);

			if (!name.empty() && !HasAction(edit.actions, name))
			{
				edit.folders.Move(name, edit.folders.FolderOf(action.name));
				action.name = name;
				edit.changed = true;
			};

			m_renaming.clear();
		};

		ImGui::TableNextColumn();
		edit.changed |= DrawKey("##Key", action.key);

		ImGui::TableNextColumn();
		edit.changed |= DrawKey("##Alt", action.alt);

		ImGui::TableNextColumn();

		if (ImGui::SmallButton("Remove"))
		{
			edit.removed.push_back(action.name);
			edit.changed = true;
		};

		ImGui::PopID();
	};

	void KeyBindings::DrawAddItems(const std::string& folder, Edit& edit, bool menu)
	{
		if (menu ? ImGui::MenuItem("Add Action") : ImGui::Button("Add Action"))
		{
			const std::string name = FreeName(edit.actions);

			edit.actions.push_back({ name, Key::None, Key::None, folder });
			edit.folders.Move(name, folder);
			edit.changed = true;
		};

		if (!menu)
			ImGui::SameLine();

		if (menu ? ImGui::MenuItem("Add Folder") : ImGui::Button("Add Folder"))
		{
			std::string path = edit.folders.AddFolder(folder, default_folder);

			for (int number = first_number; path.empty(); number++)
				path = edit.folders.AddFolder(folder, Numbered(default_folder, number));

			edit.refiled = true;
			m_tree.Rename(path);
		};
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

	std::string KeyBindings::FreeName(const std::vector<InputActions::Action>& actions)
	{
		std::string name = default_name;

		for (int number = first_number; HasAction(actions, name); number++)
			name = Numbered(default_name, number);

		return name;
	};
};
