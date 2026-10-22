#include "KeyBindings.h"

#include "EditorGui.h"
#include "EditorIcons.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <optional>
#include <utility>


namespace Loom
{
	namespace
	{
		constexpr int columns = 3;
		constexpr const char* default_name = "Action";
		constexpr const char* default_folder = "New Folder";
		constexpr const char* key_join = "+";

		// What the second action or folder given the default name is numbered.
		constexpr int first_number = 2;

		constexpr const char* action_payload = "LoomAction";
		constexpr const char* folder_payload = "LoomActionFolder";

		// Between an action's name and where it is in the list, in what the
		// folders file it by, since actions may share a name.
		constexpr char index_mark = '\x1f';

		std::string Numbered(const char* name, int number)
		{
			return std::string(name) + " " + std::to_string(number);
		};

		bool HasAction(const std::vector<InputActions::Action>& actions, const std::string& name)
		{
			return std::any_of(actions.begin(), actions.end(), [&name](const InputActions::Action& action) { return action.name == name; });
		};

		std::string ItemOf(const std::vector<InputActions::Action>& actions, size_t index)
		{
			return actions[index].name + index_mark + std::to_string(index);
		};

		size_t IndexOf(const std::string& item)
		{
			return std::stoul(item.substr(item.rfind(index_mark) + 1));
		};

		// Wide enough for the longest key's name beside the combo's arrow.
		float KeyWidth()
		{
			float widest = 0.0f;

			for (const Key key : AllKeys())
				widest = std::max(widest, ImGui::CalcTextSize(NameOf(key).data()).x);

			constexpr float padded_sides = 2.0f;

			return widest + ImGui::GetStyle().FramePadding.x * padded_sides + ImGui::GetFrameHeight();
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
		edit.key_width = KeyWidth();

		for (const std::string& folder : InputActions::Folders())
			edit.folders.AddFolder(VirtualFolders::ParentOf(folder), VirtualFolders::NameOf(folder));

		std::vector<std::string> items{ };

		for (size_t i = 0; i < edit.actions.size(); i++)
		{
			items.push_back(ItemOf(edit.actions, i));
			edit.folders.Move(items.back(), edit.actions[i].folder);
		};

		const VirtualFolders::Node root = edit.folders.Build(items);
		const FolderTree::Hooks hooks = Hooks(edit);

		DrawAddItems("", edit, false);
		ImGui::SameLine();
		m_tree.DrawSearch();

		if (ImGui::BeginTable("Actions", columns, ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			m_tree.Draw(edit.folders, root, hooks);

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

		std::vector<std::string> folders{ };

		if (edit.refiled)
		{
			for (size_t i = 0; i < items.size(); i++)
				edit.actions[i].folder = edit.folders.FolderOf(items[i]);

			folders.assign(edit.folders.Folders().begin(), edit.folders.Folders().end());
		}
		else
			folders = InputActions::Folders();

		// From the back, so the places of those still to go hold.
		std::sort(edit.removed.rbegin(), edit.removed.rend());

		for (const size_t index : edit.removed)
			edit.actions.erase(edit.actions.begin() + (std::ptrdiff_t)index);

		if (!edit.removed.empty())
			m_renaming.reset();

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
		hooks.name = [](const std::string& item) { return item.substr(0, item.rfind(index_mark)); };
		hooks.item = [this, &edit, hooks](const std::string& item) { DrawAction(IndexOf(item), edit, hooks); };

		return hooks;
	};

	void KeyBindings::DrawAction(size_t index, Edit& edit, const FolderTree::Hooks& hooks)
	{
		InputActions::Action& action = edit.actions[index];
		const std::string item = ItemOf(edit.actions, index);

		ImGui::PushID((int)index);
		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		// The handle it is dragged and right-clicked by.
		ImGui::TreeNodeEx(
			(std::string(EditorIcons::Bindings) + "##action").c_str(),
			ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);

		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(action_payload, item.c_str(), item.size() + 1);
			ImGui::Text("%s %s", EditorIcons::Bindings, action.name.c_str());
			ImGui::EndDragDropSource();
		};

		if (ImGui::BeginDragDropTarget())
		{
			FolderTree::Drop(edit.folders, edit.folders.FolderOf(item), hooks);
			ImGui::EndDragDropTarget();
		};

		if (ImGui::BeginPopupContextItem())
		{
			if (!edit.folders.FolderOf(item).empty() && ImGui::MenuItem("Move to Top"))
			{
				edit.folders.Move(item, "");
				edit.refiled = true;
			};

			if (ImGui::MenuItem("Remove"))
			{
				edit.removed.push_back(index);
				edit.changed = true;
			};

			ImGui::EndPopup();
		};

		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);

		std::string shown = action.name;
		InputTextString("##Name", m_renaming == index ? m_name : shown);

		if (ImGui::IsItemActivated())
		{
			m_renaming = index;
			m_name = action.name;
		};

		if (ImGui::IsItemDeactivated() && m_renaming == index)
		{
			if (const std::string name = InputActions::Tidy(m_name); !name.empty() && name != action.name)
			{
				action.name = name;
				edit.changed = true;
			};

			m_renaming.reset();
		};

		ImGui::TableNextColumn();
		edit.changed |= DrawInput(action.keys, edit.key_width);

		ImGui::TableNextColumn();

		if (ImGui::SmallButton("Remove"))
		{
			edit.removed.push_back(index);
			edit.changed = true;
		};

		ImGui::PopID();
	};

	void KeyBindings::DrawAddItems(const std::string& folder, Edit& edit, bool menu)
	{
		if (menu ? ImGui::MenuItem("Add Action") : ImGui::Button("Add Action"))
		{
			// Its name matches nothing yet.
			m_tree.ClearSearch();
			edit.actions.push_back({ FreeName(edit.actions), { }, folder });
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

	bool KeyBindings::DrawInput(std::vector<Key>& keys, float key_width)
	{
		// An action with no key yet shows the one it is waiting for.
		std::vector<Key> shown = keys.empty() ? std::vector<Key>{ Key::None } : keys;
		std::optional<size_t> unset{ };
		bool changed = false;

		for (size_t i = 0; i < shown.size(); i++)
		{
			ImGui::PushID((int)i);

			if (i > 0)
			{
				ImGui::SameLine();
				ImGui::TextUnformatted(key_join);
				ImGui::SameLine();
			};

			ImGui::SetNextItemWidth(key_width);

			if (const std::optional<Key> picked = DrawKey("##Key", shown[i]))
			{
				// None takes a key out of a combination, though not its last,
				// even one not chosen yet.
				if (*picked == Key::None && shown.size() > 1)
				{
					unset = i;
					changed = true;
				}
				else if (*picked != shown[i])
				{
					shown[i] = *picked;
					changed = true;
				};
			};

			ImGui::PopID();
		};

		if (unset)
			shown.erase(shown.begin() + (std::ptrdiff_t)*unset);

		ImGui::SameLine();

		if (ImGui::SmallButton(key_join))
		{
			shown.push_back(Key::None);
			changed = true;
		};

		ImGui::SetItemTooltip("Add a key, to be held with the others");

		if (changed)
			keys = std::move(shown);

		return changed;
	};

	std::optional<Key> KeyBindings::DrawKey(const char* label, Key key)
	{
		std::optional<Key> picked{ };

		if (ImGui::BeginCombo(label, NameOf(key).data(), ImGuiComboFlags_HeightLarge))
		{
			for (const Key each : AllKeys())
				if (ImGui::Selectable(NameOf(each).data(), each == key))
					picked = each;

			ImGui::EndCombo();
		};

		return picked;
	};

	std::string KeyBindings::FreeName(const std::vector<InputActions::Action>& actions)
	{
		std::string name = default_name;

		for (int number = first_number; HasAction(actions, name); number++)
			name = Numbered(default_name, number);

		return name;
	};
};
