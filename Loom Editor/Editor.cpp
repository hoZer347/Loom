#include "Editor.h"

#include "EditorLog.h"
#include "EditorTheme.h"

#include "Audio.h"
#include "Component.h"
#include "ComponentRegistry.h"
#include "EditorGui.h"
#include "Engine.h"
#include "GameObject.h"
#include "Input.h"
#include "Jobs.h"
#include "Light.h"
#include "LoomObject.h"
#include "Material.h"
#include "Prefab.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "Textures.h"
#include "Transform.h"
#include "Ui.h"

#include "Utilities/AxisGui.h"
#include "Utilities/StateMachine.h"
#include "Utilities/StateMachineMonitor.h"

#include "EditCommands.h"
#include "EditorFileDialogs.h"
#include "EditorIcons.h"
#include "FieldNames.h"
#include "ModelImporter.h"
#include "ProjectAssets.h"
#include "ProjectTemplate.h"
#include "TextFit.h"

#include "OpenGL.h"

#include "glm/gtc/type_ptr.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>


namespace Loom
{
	namespace
	{
		constexpr float rotation_drag_speed = 0.5f;

		// The thread ID that takes a GameObject out of the update loop entirely.
		constexpr int unprocessed_thread = -1;

		constexpr int selection_frames = 60;

		constexpr ImGuiTreeNodeFlags header_flags =
			ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap;

		// A category of a component's fields, inside that component's own
		// header, folded until it is opened.
		constexpr ImGuiTreeNodeFlags field_group_flags = ImGuiTreeNodeFlags_SpanAvailWidth;

		// One thing an object's inspector draws: a field or a Button, by where
		// it is in their list, or a group of them, by where it is in its
		// parent's.
		struct FieldItem
		{
			enum Kind { Field, Button, Group };

			Kind kind = Field;
			size_t index = 0;
		};

		// Fields and Buttons by the category they were named under, nested at
		// each '/'. Everything a group holds, its own fields and Buttons and
		// the groups in it, keeps the order the first of each was declared in.
		struct FieldGroup
		{
			std::string label;
			std::vector<FieldItem> items;
			std::vector<FieldGroup> groups;
		};

		FieldGroup GroupFields(const LoomObject& object)
		{
			const std::vector<SerializedField>& fields = object.GetFields();
			const std::vector<LoomObject::FieldButton>& buttons = object.GetButtons();

			FieldGroup top;

			const auto file = [&top](std::string_view path, FieldItem item)
			{
				FieldGroup* group = &top;

				while (!path.empty())
				{
					const size_t slash = path.find('/');
					const std::string_view part = path.substr(0, slash);

					auto found = std::find_if(group->groups.begin(), group->groups.end(), [part](const FieldGroup& inner) { return inner.label == part; });

					if (found == group->groups.end())
					{
						group->items.push_back({ FieldItem::Group, group->groups.size() });
						group->groups.push_back({ std::string(part) });
						found = std::prev(group->groups.end());
					};

					group = &*found;
					path = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
				};

				group->items.push_back(item);
			};

			// Each Button in its place among the fields: after those declared before it.
			size_t button = 0;

			for (size_t i = 0; i <= fields.size(); i++)
			{
				for (; button < buttons.size() && buttons[button].fieldsBefore == i; button++)
					file(NameCategory(buttons[button].name), { FieldItem::Button, button });

				if (i < fields.size())
					file(fields[i].Category(), { FieldItem::Field, i });
			};

			return top;
		};

		// What a group holds in order, each group in it under a heading that
		// folds it open and shut.
		template <typename DrawItem>
		void DrawFieldGroup(const FieldGroup& group, const DrawItem& draw_item)
		{
			for (const FieldItem& item : group.items)
			{
				if (item.kind != FieldItem::Group)
				{
					draw_item(item);
					continue;
				};

				const FieldGroup& inner = group.groups[item.index];

				if (ImGui::TreeNodeEx(inner.label.c_str(), field_group_flags))
				{
					DrawFieldGroup(inner, draw_item);
					ImGui::TreePop();
				};
			};
		};

		bool AnyMouseClicked()
		{
			for (int button = 0; button < ImGuiMouseButton_COUNT; button++)
				if (ImGui::IsMouseClicked(button))
					return true;

			return false;
		};

		bool AnyMouseReleased()
		{
			for (int button = 0; button < ImGuiMouseButton_COUNT; button++)
				if (ImGui::IsMouseReleased(button))
					return true;

			return false;
		};

		GameObject* FindByName(GameObject& root, const std::string& name)
		{
			if (root.GetName() == name)
				return &root;

			for (GameObject* child : root.GetChildren())
				if (GameObject* found = FindByName(*child, name))
					return found;

			return nullptr;
		};

		GameObject* FindByGuid(GameObject& root, const Guid& guid)
		{
			if (root.GetGuid() == guid)
				return &root;

			for (GameObject* child : root.GetChildren())
				if (GameObject* found = FindByGuid(*child, guid))
					return found;

			return nullptr;
		};

		// Sits beside the executable and holds what the editor remembers between
		// runs. Beside the executable rather than in the working directory,
		// because opening a project moves the working directory to it.
		std::filesystem::path BesideExecutable(const char* file)
		{
			char buffer[MAX_PATH]{ };

			GetModuleFileNameA(nullptr, buffer, MAX_PATH);

			return std::filesystem::path(buffer).parent_path() / file;
		};

		std::filesystem::path LayoutsFolder()
		{
			return BesideExecutable("Layouts");
		};

		std::string SettingsPath()
		{
			return BesideExecutable("LoomEditor.settings").string();
		};

		// Where new projects go until the user points somewhere else.
		std::string DesktopPath()
		{
			PWSTR wide = nullptr;
			std::filesystem::path desktop;

			if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &wide)))
				desktop = wide;

			CoTaskMemFree(wide);

			std::error_code code;

			return desktop.empty()
				? std::filesystem::current_path(code).string()
				: desktop.string();
		};

		bool HasProjectFile(const std::filesystem::path& folder)
		{
			std::error_code code;

			for (const auto& file : std::filesystem::directory_iterator(folder, code))
				if (file.path().extension() == ProjectTemplate::extension)
					return true;

			return false;
		};

		// The nearest folder above a scene file with a project file in it, or
		// the scene's own folder when there is none.
		std::filesystem::path ProjectFolderOf(const std::filesystem::path& scene)
		{
			std::filesystem::path folder = scene.parent_path();

			while (!HasProjectFile(folder))
			{
				if (folder == folder.parent_path())
					return scene.parent_path();

				folder = folder.parent_path();
			};

			return folder;
		};

		// The projects under <engine>/Demos: each folder there with a project
		// file in it.
		std::vector<std::filesystem::path> DemoProjects()
		{
			std::vector<std::filesystem::path> demos{ };
			std::error_code code;

			for (const auto& entry : std::filesystem::directory_iterator(
				std::filesystem::path(ProjectTemplate::EngineRoot()) / "Demos", code))
				if (entry.is_directory(code) && HasProjectFile(entry.path()))
					demos.push_back(entry.path());

			std::sort(demos.begin(), demos.end());

			return demos;
		};

		// How much of a Hierarchy row, top and bottom, drops beside it rather
		// than into it.
		constexpr float sibling_edge = 0.25f;

		// A window docked on the frame it first appears only takes its tab on
		// the one after.
		constexpr int scene_focus_frames = 2;

		// How often the editor checks whether a shader file was edited outside it.
		constexpr double shader_poll_seconds = 0.5;

		// The Hierarchy's eye, against the font size: its half-width, how far
		// its lids bow from the middle, its pupil, and its lashes when closed.
		constexpr float eye_half_width = 0.42f;
		constexpr float eye_lid_bow = 0.5f;
		constexpr float eye_pupil = 0.16f;
		constexpr float eye_lash = 0.14f;
		constexpr float eye_stroke = 1.5f;
		constexpr float eye_disabled_by_parent_alpha = 0.4f;

		// Where each lash leaves the lower lid, as a fraction of its width.
		constexpr float eye_lash_positions[] = { -0.5f, 0.0f, 0.5f };

		// Where the eye goes on the Hierarchy row about to be drawn: square, at
		// its right end.
		ImRect EyeRect()
		{
			const float size = ImGui::GetFontSize();
			const ImVec2 start = ImGui::GetCursorScreenPos();
			const float right = start.x + ImGui::GetContentRegionAvail().x;

			return ImRect(ImVec2(right - size, start.y), ImVec2(right, start.y + size));
		};

		void DrawEye(ImDrawList* draw, ImVec2 centre, float size, bool open, ImU32 colour)
		{
			const float half = size * eye_half_width;
			const float bow = half * eye_lid_bow;
			const ImVec2 left(centre.x - half, centre.y);
			const ImVec2 right(centre.x + half, centre.y);

			// A cubic's control points sit 4/3 out from where its middle passes.
			constexpr float control = 4.0f / 3.0f;
			constexpr float control_x = 0.5f;

			const auto lid =
				[&](float side)
				{
					draw->PathLineTo(left);
					draw->PathBezierCubicCurveTo(
						ImVec2(centre.x - half * control_x, centre.y + side * bow * control),
						ImVec2(centre.x + half * control_x, centre.y + side * bow * control),
						right);
					draw->PathStroke(colour, ImDrawFlags_None, eye_stroke);
				};

			lid(1.0f);

			if (open)
			{
				lid(-1.0f);
				draw->AddCircleFilled(centre, size * eye_pupil, colour);
				return;
			};

			// The closed lid is the lower one, so its lashes hang below it.
			for (float position : eye_lash_positions)
			{
				const ImVec2 root(
					centre.x + half * position,
					centre.y + bow * (1.0f - position * position));

				draw->AddLine(
					root,
					ImVec2(root.x + position * size * eye_lash, root.y + size * eye_lash),
					colour,
					eye_stroke);
			};
		};

		// GLFW's drop callback carries no user data, and there is one editor.
		Editor* drop_receiver = nullptr;

		void OnDrop(GLFWwindow* window, int count, const char** paths)
		{
			// The OS swallows mouse moves while it drags, so ImGui still thinks
			// the cursor is wherever it left the window. With viewports on,
			// ImGui works in screen coordinates rather than the window's.
			double x = 0.0;
			double y = 0.0;
			glfwGetCursorPos(window, &x, &y);

			if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			{
				int left = 0;
				int top = 0;
				glfwGetWindowPos(window, &left, &top);

				x += left;
				y += top;
			};

			for (int i = 0; i < count; i++)
				drop_receiver->Drop(paths[i], (float)x, (float)y);
		};

		// On every OS window ImGui has, since a panel dragged out of the main
		// window takes drops in a window of its own.
		void SetDropCallback(GLFWdropfun callback)
		{
			for (ImGuiViewport* viewport : ImGui::GetPlatformIO().Viewports)
				if (viewport->PlatformHandle)
					glfwSetDropCallback((GLFWwindow*)viewport->PlatformHandle, callback);
		};

		// What is docked in the middle: the views, and the shader and sprite
		// editors.
		constexpr const char* centre_windows[] = { EditorPanels::SceneId, EditorPanels::Game, EditorPanels::Debug, EditorPanels::PrefabId, EditorPanels::Shaders, EditorPanels::Sprites };

		// What lights the Prefab panel, which the Hierarchy leaves out.
		constexpr const char* prefab_light = "Prefab Light";

		// Panels docked as a tab beside another, each with the one it goes beside.
		constexpr std::pair<const char*, const char*> side_windows[] = { { EditorPanels::Scripts, EditorPanels::Project }, { EditorPanels::TwoD, EditorPanels::Project }, { EditorPanels::KeyBindings, EditorPanels::Console } };

		// The ImGui settings entry that holds which panels are open.
		constexpr const char* panel_settings = "LoomEditor";

		constexpr ImGuiWindowFlags view_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;

		constexpr const char* camera_controls =
			"Right mouse: look, with WASD and QE to fly (Shift for faster)\n"
			"Middle mouse: pan\n"
			"Wheel: in and out";

		// Inspector field names longer than this are cut, however wide the panel.
		constexpr size_t max_label_chars = 20;

		// The most of the Inspector's width the label column may take before
		// the labels start shrinking with it.
		constexpr float max_label_share = 0.4f;

		// Significant digits with no trailing zeros, so 1 draws as 1 rather than 1.000.
		constexpr const char* float_format = "%.7g";
		constexpr const char* double_format = "%.15g";

		constexpr float drag_speed = 0.01f;

		// Cut to max_label_chars, ellipsis included, then further until it fits in width.
		std::string Shortened(const std::string& text, float width)
		{
			if (text.size() <= max_label_chars && ImGui::CalcTextSize(text.c_str()).x <= width)
				return text;

			std::string shown = text.substr(0, max_label_chars - strlen(ellipsis));

			while (!shown.empty() && ImGui::CalcTextSize((shown + ellipsis).c_str()).x > width)
				shown.pop_back();

			while (!shown.empty() && shown.back() == ' ')
				shown.pop_back();

			return shown + ellipsis;
		};

		// Labels sit left of their widgets, Unity-style, in a column as wide as
		// the longest one and no wider. longest seeds it with any label drawn
		// beside the fields.
		float LabelColumn(const std::vector<std::string>& labels, float longest)
		{
			for (const std::string& label : labels)
				longest = (std::max)(longest, ImGui::CalcTextSize(Shortened(label, FLT_MAX).c_str()).x);

			return (std::min)(longest, ImGui::GetContentRegionAvail().x * max_label_share);
		};

		// The label column for an object's fields: wide enough for each label as
		// far in as its categories nest it.
		float FieldLabelColumn(const LoomObject& object, const std::vector<std::string>& labels)
		{
			const std::vector<SerializedField>& fields = object.GetFields();
			float longest = 0.0f;

			for (size_t i = 0; i < labels.size(); i++)
			{
				const std::string_view category = fields[i].Category();
				const float depth = category.empty() ? 0.0f : (float)(std::count(category.begin(), category.end(), '/') + 1);
				const float width = ImGui::CalcTextSize(Shortened(labels[i], FLT_MAX).c_str()).x;

				longest = (std::max)(longest, width + depth * ImGui::GetStyle().IndentSpacing);
			};

			return (std::min)(longest, ImGui::GetContentRegionAvail().x * max_label_share);
		};

		// Draws text in the label column and leaves the cursor where its widget goes.
		void Label(const std::string& text, float column)
		{
			const float left = ImGui::GetCursorPosX();
			const std::string shown = Shortened(text, column);

			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(shown.c_str());

			if (shown != text && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", text.c_str());

			ImGui::SameLine(left + column + ImGui::GetStyle().ItemInnerSpacing.x);
		};

		void DragSource(const LoomObject& object)
		{
			if (!ImGui::BeginDragDropSource())
				return;

			ImGui::SetDragDropPayload(Editor::object_payload, &object.GetGuid(), sizeof(Guid));
			ImGui::TextUnformatted(object.NameAndID().c_str());

			ImGui::EndDragDropSource();
		};

		// The object being dragged, as the field would hold it. Null when nothing
		// is being dragged or the field has no place for it, which also keeps the
		// field from lighting up under a drag it would refuse.
		LoomObject* DraggedInto(const SerializedField& field)
		{
			const ImGuiPayload* payload = ImGui::GetDragDropPayload();

			if (payload == nullptr || !payload->IsDataType(Editor::object_payload))
				return nullptr;

			return field.ReferenceFor(LoomObject::GetByGuid(*(const Guid*)payload->Data));
		};

		// Components carry no name of their own, so one is shown as its type on
		// the GameObject that holds it.
		std::string Describe(const LoomObject& object)
		{
			if (const ComponentBase* component = dynamic_cast<const ComponentBase*>(&object))
				return ComponentRegistry::NameOf(*component) + " on " + component->GetGameObject()->NameAndID();

			return object.NameAndID();
		};

		// Whether the field was cleared or given something dropped on it.
		bool DrawReferenceField(const SerializedField& field)
		{
			LoomObject* target = field.GetReference();

			const std::string shown = (target ? Describe(*target) : "none") + "###target";

			const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
			const float clear_width = ImGui::GetFrameHeight();

			bool changed = false;

			// A button for its frame: something the width of the other fields to
			// drop onto, sharing that width with the clear button when there is
			// something to clear.
			ImGui::Button(
				shown.c_str(),
				ImVec2(ImGui::CalcItemWidth() - (target ? clear_width + spacing : 0.0f), 0.0f));

			if (LoomObject* dropped = DraggedInto(field))
				if (ImGui::BeginDragDropTarget())
				{
					if (ImGui::AcceptDragDropPayload(Editor::object_payload))
					{
						field.SetReference(dropped);
						changed = true;
					};

					ImGui::EndDragDropTarget();
				};

			if (target)
			{
				ImGui::SameLine(0.0f, spacing);

				if (ImGui::Button("x", ImVec2(clear_width, 0.0f)))
				{
					field.SetReference(nullptr);
					changed = true;
				};
			};

			return changed;
		};

		// An enum is stored as its underlying integer, which is one of these.
		long long IntegerOf(const SerializedField& field)
		{
			switch (field.type)
			{
			case FieldType::Int:	return *(int*)field.data;
			case FieldType::UInt:	return *(unsigned int*)field.data;
			default:				return *(long long*)field.data;
			};
		};

		void SetInteger(const SerializedField& field, long long value)
		{
			switch (field.type)
			{
			case FieldType::Int:	*(int*)field.data = (int)value;						break;
			case FieldType::UInt:	*(unsigned int*)field.data = (unsigned int)value;	break;
			default:				*(long long*)field.data = value;					break;
			};
		};

		// Whether another of the object's choices was picked, a value indexing
		// choices. A value that is none of them shows as its number.
		bool DrawChoiceField(const char* label, const SerializedField& field, const std::vector<std::string>& choices)
		{
			const long long value = IntegerOf(field);
			const bool known = value >= 0 && value < (long long)choices.size();
			const std::string shown = known ? choices[(size_t)value] : std::to_string(value);

			bool changed = false;

			if (ImGui::BeginCombo(label, shown.c_str()))
			{
				for (size_t i = 0; i < choices.size(); i++)
					if (ImGui::Selectable(choices[i].c_str(), (long long)i == value) && (long long)i != value)
					{
						SetInteger(field, (long long)i);
						changed = true;
					};

				ImGui::EndCombo();
			};

			return changed;
		};

		// Whether another enumerator was picked. A value that is none of them
		// shows as its number.
		bool DrawEnumField(const char* label, const SerializedField& field, const std::vector<FieldNames::Enumerator>& enumerators)
		{
			const long long value = IntegerOf(field);

			std::string shown = std::to_string(value);

			for (const FieldNames::Enumerator& enumerator : enumerators)
				if (enumerator.value == value)
					shown = enumerator.label;

			bool changed = false;

			if (ImGui::BeginCombo(label, shown.c_str()))
			{
				for (const FieldNames::Enumerator& enumerator : enumerators)
					if (ImGui::Selectable(enumerator.label.c_str(), enumerator.value == value) &&
						enumerator.value != value)
					{
						SetInteger(field, enumerator.value);
						changed = true;
					};

				ImGui::EndCombo();
			};

			return changed;
		};
	};

	Editor::Editor() :
		m_dialogs(std::make_unique<EditorFileDialogs>())
	{
		EditorLog::Get().Install();


		// The editor draws the scene into its own viewport, so the engine has to
		// stop drawing it over the whole window, and updating becomes something
		// the play controls own rather than something that always happens.
		Engine::renderScenes = false;
		Engine::updateScenes = false;

		Engine::SetGuiFunction([this]() { DrawGui(); });

		// Set before the first frame, which is when ImGui reads it.
		static const std::string layout = BesideExecutable("LoomEditor.layout.ini").string();
		ImGui::GetIO().IniFilename = layout.c_str();
		AddPanelSettings();

		// The engine keeps every floating window in an OS window of its own,
		// which for an editor means dialogs that sit behind the main window and
		// take clicks with them. Floating windows belong inside the editor
		// until the user drags one out.
		ImGui::GetIO().ConfigViewportsNoAutoMerge = false;

		// Clicking a number field types into it, Unity-style; dragging still scrubs.
		ImGui::GetIO().ConfigDragClickToInputText = true;

		ImGui::GetPlatformIO().Platform_OpenInShellFn = OpenInShell;
		ImGui::GetPlatformIO().Platform_OpenInShellUserData = this;

		drop_receiver = this;

		LoadSettings();
		m_settings.Apply();
	};

	Editor::~Editor()
	{
		// The handler reads this editor's panels, and ImGui would otherwise
		// call it when the engine destroys the context, after this is gone.
		// Saving before ImGui has read the file would leave only the panels in it.
		ImGuiIO& io = ImGui::GetIO();

		if (ImGui::GetCurrentContext()->SettingsLoaded)
			ImGui::SaveIniSettingsToDisk(io.IniFilename);

		io.IniFilename = nullptr;
		ImGui::RemoveSettingsHandler(panel_settings);

		SetDropCallback(nullptr);
		drop_receiver = nullptr;

		Engine::SetGuiFunction(Task{ });

		Engine::renderScenes = true;
		Engine::updateScenes = true;

		// What loading queued lands before the objects it refers to go, and
		// what deleting them queues lands while the script library its code is
		// in is still loaded. A run that never reached its first frame, such as
		// one stopped by a bad argument, still has all of it queued.
		ClosePrefab();
		Engine::DoTasks();

		for (Scene* scene : m_ownedScenes)
			delete scene;

		m_ownedScenes.clear();

		Engine::DoTasks();

		EditorLog::Get().Uninstall();
	};

	Backend Editor::SavedBackend()
	{
		std::ifstream in(SettingsPath());
		std::string line;
		EditorSettings settings;

		while (std::getline(in, line))
		{
			while (!line.empty() && line.back() == '\r')
				line.pop_back();

			settings.Read(line);
		};

		return settings.backend;
	};

	void Editor::LoadSettings()
	{
		std::ifstream in(SettingsPath());

		if (!in)
			return;

		std::string line, project;
		bool first = true;

		while (std::getline(in, line))
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
				line.pop_back();

			// A settings file saved by hand through a text editor can start with
			// a byte order mark, and nothing here would match the key behind it.
			if (first && line.rfind("\xEF\xBB\xBF", 0) == 0)
				line.erase(0, 3);

			first = false;

			if (line.rfind("project=", 0) == 0)
				project = line.substr(strlen("project="));

			// Read before the project opens, because opening it is what picks a
			// scene to start on.
			if (line.rfind("scene=", 0) == 0)
				m_startupScene = line.substr(strlen("scene="));

			if (line.rfind("layout=", 0) == 0)
				m_currentLayout = line.substr(strlen("layout="));

			m_settings.Read(line);
		};

		std::error_code code;

		if (!project.empty() && std::filesystem::is_directory(project, code))
			OpenProject(project);
	};

	void Editor::SaveSettings() const
	{
		std::ofstream out(SettingsPath());

		if (!out)
			return;

		out << "project=" << m_projectPath << '\n';
		m_settings.Write(out);

		// So the editor comes back up on the scene that was open, rather than on
		// whichever of the project's scenes sorts first. Written from what the
		// session last made active rather than from what is open right now: a
		// rebuild saves with every scene closed, and that is not the user
		// leaving the scene behind.
		if (!m_startupScene.empty())
			out << "scene=" << m_startupScene << '\n';

		if (!m_currentLayout.empty())
			out << "layout=" << m_currentLayout << '\n';
	};

	std::string Editor::StartupScene() const
	{
		std::error_code code;

		// The one the last session left open, as long as it is still there and
		// still belongs to this project; otherwise the first the project has.
		if (!m_startupScene.empty() &&
			m_startupScene.rfind(m_projectPath, 0) == 0 &&
			std::filesystem::exists(m_startupScene, code))
			return m_startupScene;

		return m_projectScenes.empty()
			? ""
			: m_projectScenes.front();
	};

	bool Editor::OpenProject(const std::string& path, bool open_scene)
	{
		std::error_code code;

		// A project file stands for the folder it is in, and is given scripts to
		// go with it if it has none.
		if (std::filesystem::path(path).extension() == ProjectTemplate::extension &&
			std::filesystem::is_regular_file(path, code))
		{
			std::string error;

			if (ProjectTemplate::EnsureScripts(path, &error).empty())
				std::cerr << "No scripts project for " << path << ": " << error << std::endl;

			return OpenProject(std::filesystem::absolute(path, code).parent_path().string(), open_scene);
		};

		// So does the Key Bindings panel's file, which shows the panel.
		if (std::filesystem::path(path).extension() == InputActions::extension &&
			std::filesystem::is_regular_file(path, code))
		{
			m_showKeyBindings = true;

			return OpenProject(std::filesystem::absolute(path, code).parent_path().string(), open_scene);
		};

		// So does the Scripts panel's file, which brings the panel forward.
		if (std::filesystem::path(path).extension() == ScriptFolders::extension &&
			std::filesystem::is_regular_file(path, code))
		{
			m_showScripts = true;
			m_focusScripts = true;

			return OpenProject(std::filesystem::absolute(path, code).parent_path().string(), open_scene);
		};

		// And the 2D panel's.
		if (std::filesystem::path(path).extension() == TwoDFolders::extension &&
			std::filesystem::is_regular_file(path, code))
		{
			m_showTwoD = true;
			m_focusTwoD = true;

			return OpenProject(std::filesystem::absolute(path, code).parent_path().string(), open_scene);
		};

		// So does a scene file, for the project it belongs to, which then starts
		// on that scene.
		if (std::filesystem::path(path).extension() == SceneSerializer::extension &&
			std::filesystem::is_regular_file(path, code))
		{
			const std::filesystem::path scene = std::filesystem::absolute(path, code).lexically_normal();

			m_startupScene = scene.string();

			if (!OpenProject(ProjectFolderOf(scene).string(), open_scene))
				return false;

			// A project that was already open kept its scenes rather than
			// starting on this one.
			if (open_scene && !m_scripts.IsBuilding())
			{
				const auto open = std::find_if(m_scenePaths.begin(), m_scenePaths.end(),
					[&](const auto& entry) { return entry.second == m_startupScene; });

				if (open == m_scenePaths.end())
					OpenScene(m_startupScene);
				else
					SetActiveScene(open->first);
			};

			return true;
		};

		if (!std::filesystem::is_directory(path, code))
		{
			std::cerr << "'" << path << "' is not a folder" << std::endl;
			return false;
		};

		const std::string resolved =
			std::filesystem::absolute(path, code).lexically_normal().string();

		// Already open: pick up new files, but leave the scenes and the script
		// library they are made of alone.
		if (resolved == m_projectPath)
		{
			RefreshProjectAssets();

			// A project file that was not there when this folder was opened -
			// New Project writes one into the folder the editor is already
			// sitting in - is picked up rather than left unregistered.
			if (!m_scripts.HasProject())
				LoadProjectFile();

			if (open_scene && !m_scripts.IsBuilding() && m_ownedScenes.empty())
				if (const std::string scene = StartupScene(); !scene.empty())
					OpenScene(scene);

			return true;
		};

		// Whatever was running, or was waiting on a build to run, belonged to the
		// project being left behind; there is nothing here to go back to.
		Engine::updateScenes = false;
		m_playing = false;
		m_playAfterCompile = false;
		m_playSnapshots.clear();
		m_snapshots.clear();

		Audio::Unload();

		CloseAllScenes();
		ClosePrefab();
		m_prefabs.clear();
		m_linkedInstances.clear();

		m_projectPath = resolved;

		// Asset paths in scenes and scripts are relative to the project, so the
		// project is where the process works from.
		std::filesystem::current_path(m_projectPath, code);

		LoadProjectFile();

		RefreshProjectAssets();
		SaveSettings();

		std::cout
			<< "Scene folder: " << m_projectPath
			<< " (" << m_projectScenes.size() << " scene(s))" << std::endl;

		// Nothing open yet and something to open: start on the scene the last
		// session was on. Not while a build is running, though: PollScripts
		// opens it once the component types the scene names actually exist.
		if (open_scene && !m_scripts.IsBuilding() && m_ownedScenes.empty())
			if (const std::string scene = StartupScene(); !scene.empty())
				OpenScene(scene);

		return true;
	};

	Scene* Editor::CreateScene(const std::string& name)
	{
		Scene* scene = new Scene(name);

		m_ownedScenes.push_back(scene);
		m_scenePaths[scene] = "";
		m_historyDirty = true;

		SetActiveScene(scene);
		Select(&scene->GetRoot());

		return scene;
	};

	Scene* Editor::OpenScene(const std::string& path)
	{
		std::string error;

		if (m_scripts.IsBuilding())
			std::cerr
				<< "Opening a scene while the scripts are compiling: any component "
				   "from them will be missing until it is opened again." << std::endl;

		Scene* scene = SceneSerializer::LoadFromFile(path, &error);

		if (scene == nullptr)
		{
			std::cerr << "Could not open scene: " << error << std::endl;
			return nullptr;
		};

		m_ownedScenes.push_back(scene);
		m_historyDirty = true;
		m_scenePaths[scene] = std::filesystem::path(path).lexically_normal().string();

		SetActiveScene(scene);

		// A model being previewed stays in the Inspector.
		if (m_selectedModel.empty())
			Select(&scene->GetRoot());

		std::cout
			<< "Opened '" << path << "' as "
			<< scene->GetName() << '(' << scene->GetGuid().ToString() << ')' << std::endl;

		return scene;
	};

	Scene* Editor::OpenSceneAlone(const std::string& path)
	{
		if (!m_scripts.IsIdle())
		{
			std::cerr << "The scenes are down while the scripts build; open it once they are back." << std::endl;
			return nullptr;
		};

		Stop();

		const std::string normal = std::filesystem::path(path).lexically_normal().string();

		if (m_ownedScenes.size() == 1 && ScenePathOf(m_ownedScenes.front()) == normal)
		{
			SetActiveScene(m_ownedScenes.front());
			return m_ownedScenes.front();
		};

		for (Scene* scene : m_ownedScenes)
			if (!SaveToProject(scene))
			{
				std::cerr << "'" << scene->GetName() << "' did not save, so it stays open." << std::endl;
				return nullptr;
			};

		const std::vector<Scene*> closing = m_ownedScenes;

		for (Scene* scene : closing)
			CloseScene(scene);

		RefreshProjectAssets();

		return OpenScene(normal);
	};

	bool Editor::SaveScene(Scene* scene, const std::string& path)
	{
		if (scene == nullptr)
			return false;

		std::string error;

		if (!SceneSerializer::SaveToFile(*scene, path, &error))
		{
			std::cerr << "Could not save scene: " << error << std::endl;
			return false;
		};

		m_scenePaths[scene] = std::filesystem::path(path).lexically_normal().string();

		std::cout << "Saved '" << scene->GetName() << "' to " << path << std::endl;

		return true;
	};

	void Editor::SaveActiveScene()
	{
		if (m_activeScene == nullptr)
			return;

		if (m_playing)
		{
			std::cerr << "Stop before saving: the scene is holding what the run has done to it" << std::endl;
			return;
		};

		if (SaveToProject(m_activeScene))
			RefreshProjectAssets();
	};

	bool Editor::SaveToProject(Scene* scene)
	{
		std::string path = m_scenePaths[scene];

		if (path.empty())
		{
			if (m_projectPath.empty())
			{
				std::cerr << "Save Scene needs a project to put '" << scene->GetName() << "' in" << std::endl;
				return false;
			};

			const std::filesystem::path folder = std::filesystem::path(m_projectPath) / ProjectTemplate::scenes_folder;
			std::filesystem::path file = folder / (scene->GetName() + SceneSerializer::extension);

			// A scene of the same name already saved keeps its file.
			std::error_code code;

			for (int copy = 2; std::filesystem::exists(file, code); copy++)
				file = folder / (scene->GetName() + " " + std::to_string(copy) + SceneSerializer::extension);

			path = file.string();
		};

		return SaveScene(scene, path);
	};

	bool Editor::SaveProject()
	{
		// Scenes Stop or a build just put back are only whole once the queue
		// has run, since AddChild defers.
		Engine::DoTasks();

		bool saved = true;

		for (Scene* scene : m_ownedScenes)
			saved = SaveToProject(scene) && saved;

		saved = m_shaders.SaveAll() && saved;
		saved = m_sprites.SaveAll() && saved;

		// An active scene saved for the first time just now is the one the
		// next session starts on.
		SetActiveScene(m_activeScene);

		return saved;
	};

	void Editor::FullRefresh()
	{
		if (!IsRefreshing())
			m_refresh.Start();
	};

	void Editor::PollRefresh()
	{
		// Mid-build the scenes are down, held as text until the scripts are
		// back, and there is nothing to save until they are.
		if (!m_scripts.IsIdle() || m_refresh.Poll() != EditorRefresh::Progress::Finished)
			return;

		if (!m_refresh.Succeeded())
		{
			std::cerr << "Full refresh: the editor did not build, so this one carries on." << std::endl;
			return;
		};

		Stop();

		if (!SaveProject())
		{
			std::cerr << "Full refresh: not everything saved, so this editor stays up rather than lose it." << std::endl;
			return;
		};

		m_relaunch = true;
		Engine::Stop();
	};

	std::string Editor::RelaunchTarget() const
	{
		const auto scene = m_activeScene ? m_scenePaths.find(m_activeScene) : m_scenePaths.end();

		return scene != m_scenePaths.end() ? scene->second : m_projectPath;
	};

	bool Editor::IsOwned(Scene* scene) const
	{
		return std::find(
			m_ownedScenes.begin(),
			m_ownedScenes.end(),
			scene) != m_ownedScenes.end();
	};

	std::string Editor::ScenePathOf(Scene* scene) const
	{
		const auto path = m_scenePaths.find(scene);

		return path == m_scenePaths.end() ? "" : path->second;
	};

	void Editor::CloseScene(Scene* scene)
	{
		if (scene == nullptr)
			return;

		m_ownedScenes.erase(
			std::remove(
				m_ownedScenes.begin(),
				m_ownedScenes.end(),
				scene),
			m_ownedScenes.end());

		if (m_activeScene == scene)
			m_activeScene = nullptr;

		if (m_selected && IsInHierarchy(scene->GetRoot(), m_selected))
			Select(nullptr);

		m_scenePaths.erase(scene);
		m_history.Forget(scene->GetGuid());

		// Deleting a scene mid-GUI would leave the rest of the frame walking a
		// dangling pointer; deferring puts the delete (and the unregistering it
		// queues in turn) at the top of the next frame instead.
		Engine::QueueTask([scene]() { delete scene; });
	};

	void Editor::SetActiveScene(Scene* scene)
	{
		m_activeScene = scene;

		// Closing scenes for a rebuild is not the user leaving a scene, so only
		// arriving at one is worth remembering.
		if (scene == nullptr || m_projectPath.empty())
			return;

		const auto path = m_scenePaths.find(scene);

		if (path != m_scenePaths.end() && !path->second.empty())
			m_startupScene = path->second;

		SaveSettings();
	};

	void Editor::ShowInSceneView(Scene* scene)
	{
		SetActiveScene(scene);

		m_showScene = true;
		m_sceneFocusFrames = scene_focus_frames;
	};

	void Editor::OpenPrefab(const std::string& path)
	{
		// Closing the one open clears the path this may have been handed.
		const std::string opening = path;

		const Prefabs::ObjectNode* prefab = LoadPrefab(opening);

		if (prefab == nullptr)
		{
			std::cerr << "There is no prefab at " << opening << std::endl;
			return;
		};

		const std::string text = Prefabs::Write(*prefab);

		ClosePrefab();

		m_prefabScene = new Scene(std::filesystem::path(opening).stem().string());
		Scene::Unlist(m_prefabScene);

		std::string error;
		GameObject* root = SceneSerializer::Deserialize(text, m_prefabScene->GetRoot(), &error);

		if (root == nullptr)
		{
			std::cerr << "Could not open prefab " << opening << ": " << error << std::endl;
			ClosePrefab();
			return;
		};

		m_prefabScene->AddChild(prefab_light)->Attach<Light>();

		m_prefabRoot = root->GetGuid();
		m_prefabPath = opening;

		// Agreed with the file on the next pass, once the copy has its children
		// and serializes the way it will from then on.
		m_prefabShape.clear();
		m_historyDirty = true;

		m_showPrefab = true;
		m_prefabFocusFrames = scene_focus_frames;

		Select(root);
	};

	void Editor::ClosePrefab()
	{
		if (m_prefabScene == nullptr)
			return;

		if (m_selected && IsInHierarchy(m_prefabScene->GetRoot(), m_selected))
			Select(nullptr);

		// Deferred, as CloseScene's is, past whatever the frame still draws.
		Scene* scene = std::exchange(m_prefabScene, nullptr);
		Engine::QueueTask([scene]() { delete scene; });

		m_prefabRoot = Guid();
		m_prefabPath.clear();
		m_prefabShape.clear();
		m_reshowPrefab = false;
	};

	GameObject* Editor::GetPrefabRoot() const
	{
		GameObject* root = m_prefabScene ? LoomObject::GetByGuid<GameObject>(m_prefabRoot) : nullptr;

		return root && IsInHierarchy(m_prefabScene->GetRoot(), root) ? root : nullptr;
	};

	void Editor::Select(GameObject* gameObject)
	{
		m_selected = gameObject;
		m_nameBufferOwner = nullptr;
		m_selectedModel.clear();
		m_renaming = false;
	};

	void Editor::SelectModel(const std::string& path)
	{
		Select(nullptr);
		m_selectedModel = path;
		m_selectedAsset = std::filesystem::path(path).lexically_normal().string();
	};

	void Editor::DeleteSelected()
	{
		if (!EditCommands::CanTake(m_selected))
			return;

		m_selected->Destroy();
		Select(nullptr);

		m_historyDirty = true;
	};

	void Editor::Copy()
	{
		if (EditCommands::CanTake(m_selected))
			ImGui::SetClipboardText(SceneSerializer::Serialize(*m_selected).c_str());
	};

	void Editor::Cut()
	{
		Copy();
		DeleteSelected();
	};

	void Editor::Paste()
	{
		GameObject* parent = EditCommands::PasteParent(m_selected, m_activeScene);
		const char* text = ImGui::GetClipboardText();

		if (parent == nullptr || text == nullptr)
			return;

		std::string error;

		GameObject* pasted = SceneSerializer::Deserialize(text, *parent, &error);

		if (pasted == nullptr)
		{
			std::cerr << "Nothing to paste: " << error << std::endl;
			return;
		};

		Select(pasted);

		m_historyDirty = true;
	};

	void Editor::Undo()
	{
		if (CanStepHistory() &&
			m_history.Undo(m_ownedScenes, [this](Scene* scene, const std::string& text) { return ReplaceScene(scene, text); }))
			SyncSceneCopies();
	};

	void Editor::Redo()
	{
		if (CanStepHistory() &&
			m_history.Redo(m_ownedScenes, [this](Scene* scene, const std::string& text) { return ReplaceScene(scene, text); }))
			SyncSceneCopies();
	};

	bool Editor::CanStepHistory() const
	{
		// Mid-build the scenes are down, and every step would look like it
		// belonged to a closed scene.
		return !m_playing && m_scripts.IsIdle();
	};

	void Editor::RecordHistory()
	{
		m_historyDirty = false;

		if (m_playing)
			return;

		SyncSceneCopies();
		SyncPrefabs();
		m_history.Record(m_ownedScenes);
	};

	void Editor::SyncSceneCopies()
	{
		// AddChild and Destroy defer; the text has to show where they land.
		Engine::DoTasks();

		std::unordered_map<Guid, std::vector<Scene*>> copies{ };

		for (Scene* scene : m_ownedScenes)
			copies[scene->GetGuid()].push_back(scene);

		std::erase_if(m_copyTexts,
			[&](const auto& entry)
			{
				const auto open = copies.find(entry.first);

				return open == copies.end() || open->second.size() < 2;
			});

		for (const auto& [guid, scenes] : copies)
		{
			if (scenes.size() < 2)
				continue;

			std::vector<std::string> texts{ };

			for (Scene* scene : scenes)
				texts.push_back(SceneSerializer::Serialize(*scene));

			// A scene seen open twice for the first time has nothing to have
			// changed from, so the copy opened first is the scene as it stands.
			std::string& agreed = m_copyTexts[guid];

			const auto edited = std::find_if(texts.begin(), texts.end(),
				[&](const std::string& text) { return text != agreed; });

			if (edited == texts.end())
				continue;

			agreed = *edited;

			for (size_t i = 0; i < scenes.size(); i++)
				if (texts[i] != agreed)
					ReplaceScene(scenes[i], agreed);
		};
	};

	Scene* Editor::ReplaceScene(Scene* scene, const std::string& text)
	{
		const auto slot = std::find(m_ownedScenes.begin(), m_ownedScenes.end(), scene);

		// Looked for again under the replacement alone: another copy of this
		// scene holds objects with the same guids, and a selection in one of
		// those stays where it is.
		const bool selected_here = m_selected && IsInHierarchy(scene->GetRoot(), m_selected);
		const Guid selected = selected_here ? m_selected->GetGuid() : Guid();
		const bool active = scene == m_activeScene;
		const std::string path = m_scenePaths[scene];

		m_scenePaths.erase(scene);

		if (active)
			m_activeScene = nullptr;

		if (selected_here)
			Select(nullptr);

		std::string error;

		Scene* replacement = SceneHistory::Replace(scene, text, &error);

		if (replacement == nullptr)
		{
			m_ownedScenes.erase(slot);

			std::cerr << "Could not put a scene back: " << error << std::endl;
			return nullptr;
		};

		*slot = replacement;
		m_scenePaths[replacement] = path;

		if (active)
			m_activeScene = replacement;

		if (selected_here)
			Select(FindByGuid(replacement->GetRoot(), selected));

		return replacement;
	};

	void Editor::HandleShortcuts()
	{
		// Wherever the keyboard is: it is about the whole editor.
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_R))
			FullRefresh();

		// The Sprites panel has the keyboard, and none of the scene's
		// shortcuts mean anything there.
		if (m_showSprites && m_sprites.IsFocused())
			return;

		switch (EditCommands::FromShortcut(m_showHierarchy && m_hierarchyFocused))
		{
		case EditCommand::Undo:		Undo();				break;
		case EditCommand::Redo:		Redo();				break;
		case EditCommand::Cut:		Cut();				break;
		case EditCommand::Copy:		Copy();				break;
		case EditCommand::Paste:	Paste();			break;
		case EditCommand::Delete:	DeleteSelected();	break;
		case EditCommand::None:							break;
		case EditCommand::Save:
			if (!(m_showShaders && m_shaders.IsFocused()))
				SaveActiveScene();
			break;
		};
	};

	bool Editor::IsInHierarchy(const GameObject& root, const GameObject* target)
	{
		if (&root == target)
			return true;

		for (const GameObject* child : root.GetChildren())
			if (IsInHierarchy(*child, target))
				return true;

		return false;
	};

	void Editor::RequestSelection(const std::string& name)
	{
		m_pendingSelection = name;
		m_selectionFramesLeft = selection_frames;
	};

	void Editor::ValidateSelection()
	{
		const std::vector<Scene*>& scenes = Scene::GetScenes();

		// The scenes load once the scripts have built, and add their children
		// through the task queue, so the name is looked for over a few frames
		// after the build before it is given up on.
		if (!m_pendingSelection.empty() && !m_scripts.IsBuilding())
		{
			GameObject* named = nullptr;

			for (Scene* scene : scenes)
				if ((named = FindByName(scene->GetRoot(), m_pendingSelection)))
					break;

			if (named)
			{
				Select(named);
				m_pendingSelection.clear();
			}
			else if (--m_selectionFramesLeft <= 0)
			{
				std::cerr << "No GameObject named '" << m_pendingSelection << "'" << std::endl;
				m_pendingSelection.clear();
			};
		};

		if (m_activeScene &&
			std::find(scenes.begin(), scenes.end(), m_activeScene) == scenes.end())
			m_activeScene = nullptr;

		if (m_activeScene == nullptr && !scenes.empty())
			m_activeScene = scenes.front();

		if (m_selected == nullptr)
			return;

		for (Scene* scene : scenes)
			if (IsInHierarchy(scene->GetRoot(), m_selected))
				return;

		if (m_prefabScene && IsInHierarchy(m_prefabScene->GetRoot(), m_selected))
			return;

		Select(nullptr);
	};

	void Editor::DrawGui()
	{
		// Undo a "Step": updating was switched on for one frame only.
		if (m_steppedLastFrame)
		{
			Engine::updateScenes = false;
			m_steppedLastFrame = false;
		};

		ValidateSelection();

		if (m_historyDirty && !ImGui::IsAnyItemActive() && !ImGui::IsAnyMouseDown())
			RecordHistory();

		if (!m_historySteps.empty() && m_importQueue.empty() && !m_historyDirty && CanStepHistory())
		{
			m_historySteps.front() ? Undo() : Redo();
			m_historySteps.erase(m_historySteps.begin());
		};

		HandleShortcuts();
		PollShaders();
		ReloadSavedSprites();

		// Panels come and go as OS windows, so the callback is put on whichever
		// exist this frame.
		SetDropCallback(OnDrop);

		if (!m_pendingLayout.empty())
		{
			if (EditorLayouts::Load(LayoutsFolder(), m_pendingLayout, &m_layoutWindows))
			{
				m_checkLayout = true;
				SetCurrentLayout(m_pendingLayout);
			}
			else
				std::cerr << "Could not read the layout '" << m_pendingLayout << "'" << std::endl;

			m_pendingLayout.clear();
		};

		// Nowhere the cursor can be, and nothing heard, unless the Game view
		// draws the game below and has focus.
		Input::SetView(0, 0, 0, 0);
		m_gamePanelFocused = false;
		m_gamePanelHovered = false;
		m_flyingView = false;

		if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
			m_fullscreen ? LeaveFullscreen() : EnterFullscreen(m_lastView);

		if (m_fullscreen)
			DrawFullscreen();
		else
			DrawWorkspace();

		Input::SetFocused(m_gameFocus.Update(m_gamePanelFocused, m_gamePanelHovered, AnyMouseClicked(), ImGui::IsAnyMouseDown() || AnyMouseReleased()));

		ImportQueued();
		ClickQueued();
		PollScripts();
		PollRefresh();

		// The renderer reads the window back once this frame has finished
		// drawing, so the bitmap is the whole editor.
		if (!m_screenshotPath.empty() && ++m_frame >= m_screenshotFrame)
			CaptureFrame();

		if (!m_layoutToSave.empty())
		{
			if (EditorLayouts::Save(LayoutsFolder(), m_layoutToSave))
				SetCurrentLayout(m_layoutToSave);
			else
				std::cerr << "Could not write the layout '" << m_layoutToSave << "'" << std::endl;

			m_layoutToSave.clear();
		};

		DrawFolderPrompt();
		DrawNewProjectPrompt();
		DrawSaveLayoutPrompt();
		DrawCreateAssetPrompt();
		DrawFileDialogs();

		if (m_flyingView)
			return;

		if (ImGui::IsAnyItemActive())
			m_historyDirty = true;

		for (int button = 0; button < ImGuiMouseButton_COUNT; button++)
			if (ImGui::IsMouseReleased(button))
				m_historyDirty = true;
	};

	void Editor::DrawWorkspace()
	{
		DrawMainMenuBar();

		const ImGuiID dockspace_id =
			ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

		if (m_firstFrame)
			m_checkLayout = true;

		// Only lay the panels out when there is nothing to restore: from the
		// second run on, imgui.ini holds wherever the user dragged them.
		if (m_checkLayout)
		{
			const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace_id);

			if (node == nullptr || node->IsEmpty())
				m_rebuildLayout = true;

			// A layout saved before a window existed has nowhere for it, so it
			// joins the others in the middle, or the panel it goes beside.
			else
			{
				const auto placed = [this](const char* window)
				{
					const ImGuiID id = ImHashStr(window);

					return m_layoutWindows.empty()
						? ImGui::FindWindowSettingsByID(id) != nullptr
						: std::find(m_layoutWindows.begin(), m_layoutWindows.end(), id) != m_layoutWindows.end();
				};

				if (const ImGuiDockNode* centre = ImGui::DockBuilderGetCentralNode(dockspace_id))
					for (const char* window : centre_windows)
						if (!placed(window))
							ImGui::DockBuilderDockWindow(window, centre->ID);

				for (const auto& [window, beside] : side_windows)
					if (const ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(beside));
						!placed(window) && settings != nullptr && settings->DockId != 0)
						ImGui::DockBuilderDockWindow(window, settings->DockId);
			};

			m_layoutWindows.clear();
			m_checkLayout = false;
		};

		if (m_firstFrame)
		{
			// Nothing remembered from a previous run: ask where the scenes live.
			if (m_projectPath.empty())
			{
				snprintf(
					m_folderBuffer,
					sizeof(m_folderBuffer),
					"%s",
					DesktopPath().c_str());

				m_askForFolder = true;
			};

			m_firstFrame = false;
		};

		if (m_rebuildLayout)
		{
			m_rebuildLayout = false;
			BuildDefaultLayout(dockspace_id);

			for (const Panel& panel : Panels())
				*panel.shown = panel.shownByDefault;

			SetCurrentLayout("");
		};

		if (m_showProject)		DrawProject();
		if (m_showScripts)		DrawScripts();
		if (m_showTwoD)			DrawTwoD();
		if (m_showHierarchy)	DrawHierarchy();
		if (m_showInspector)	DrawInspector();
		if (m_showScene)		DrawScene();
		DrawPrefab();
		if (m_showGame)			DrawGame();
		if (m_showDebug)		DrawDebug();
		if (m_showSceneData)	DrawSceneData();
		if (m_showConsole)		m_console.Draw(&m_showConsole);
		if (m_showShaders)		m_shaders.Draw(&m_showShaders);
		if (m_showSprites)		m_sprites.Draw(&m_showSprites);
		if (m_showStats)		DrawStats();

		if (m_showKeyBindings && m_keyBindings.Draw(&m_showKeyBindings, !m_projectFile.empty()))
			SaveInputActions();

		if (m_showImGuiDemo)	ImGui::ShowDemoWindow(&m_showImGuiDemo);

		if (m_showSettings && m_settings.Draw(&m_showSettings))
			SaveSettings();
	};

	bool Editor::TabDoubleClicked()
	{
		const ImGuiWindow* window = ImGui::GetCurrentWindow();
		const ImGuiWindow* hovered = ImGui::GetCurrentContext()->HoveredWindow;
		const ImVec2 mouse = ImGui::GetIO().MousePos;

		if (!ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			return false;

		if (!window->DockIsActive)
			return hovered == window && window->TitleBarRect().Contains(mouse);

		// The tab is drawn in the window that hosts its dock node, so that is
		// the one the cursor is on, and a hidden tab bar keeps the rect it
		// last had.
		const ImGuiDockNode* node = window->DockNode;

		return node != nullptr && !node->IsHiddenTabBar() && !node->IsNoTabBar() &&
			hovered == node->HostWindow && window->DC.DockTabItemRect.Contains(mouse);
	};

	void Editor::Watch(View view)
	{
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			m_lastView = view;

		if (TabDoubleClicked())
			EnterFullscreen(view);
	};

	void Editor::DrawFullscreen()
	{
		// How long the way back out shows for once the view fills the monitor,
		// and how it shows: large, over a dark plate, both a little see-through.
		constexpr double hint_seconds = 3.0;
		constexpr double hint_fade_seconds = 0.5;
		constexpr float hint_scale = 2.5f;
		constexpr ImVec2 hint_padding = { 32.0f, 20.0f };
		constexpr float hint_rounding = 12.0f;
		constexpr float hint_backing_alpha = 0.55f;
		constexpr float hint_text_alpha = 0.85f;
		constexpr float centred = 0.5f;
		constexpr int pushed_styles = 3;
		constexpr const char* hint_playing = "F11 to leave fullscreen";
		constexpr const char* hint_stopped = "F11 or Escape to leave fullscreen";

		// Escape is the game's while it plays.
		if (!IsPlaying() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			LeaveFullscreen();
			DrawWorkspace();
			return;
		};

		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

		const ImGuiWindowFlags flags =
			view_flags | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoSavedSettings | (m_playing ? ImGuiWindowFlags_NoNavInputs : ImGuiWindowFlags_None);

		if (ImGui::Begin("##Fullscreen", nullptr, flags))
		{
			switch (*m_fullscreen)
			{
			case View::Scene:	DrawSceneView();	break;
			case View::Prefab:	DrawPrefabView();	break;
			case View::Game:	DrawGameView();		break;
			case View::Debug:	DrawDebugView();	break;
			};

			const double shown = ImGui::GetTime() - m_fullscreenSince;

			if (shown < hint_seconds)
			{
				const float fade = (float)(std::min)(1.0, (hint_seconds - shown) / hint_fade_seconds);
				const char* hint = IsPlaying() ? hint_playing : hint_stopped;
				ImFont* font = ImGui::GetFont();
				const float font_size = ImGui::GetFontSize() * hint_scale;
				const ImVec2 text = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, hint);
				const ImVec2 middle(viewport->Pos.x + viewport->Size.x * centred, viewport->Pos.y + viewport->Size.y * centred);
				const ImVec2 corner(middle.x - text.x * centred, middle.y - text.y * centred);
				ImDrawList* draw = ImGui::GetForegroundDrawList();

				draw->AddRectFilled(
					ImVec2(corner.x - hint_padding.x, corner.y - hint_padding.y),
					ImVec2(corner.x + text.x + hint_padding.x, corner.y + text.y + hint_padding.y),
					ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, hint_backing_alpha * fade)),
					hint_rounding);
				draw->AddText(font, font_size, corner, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, hint_text_alpha * fade)), hint);
			};
		};

		ImGui::End();
		ImGui::PopStyleVar(pushed_styles);
	};

	void Editor::EnterFullscreen(View view)
	{
		m_fullscreen = view;
		m_fullscreenSince = ImGui::GetTime();
		m_windowFullscreen.Enter(Engine::window);
	};

	void Editor::LeaveFullscreen()
	{
		m_fullscreen.reset();
		m_windowFullscreen.Leave(Engine::window);
	};

	void Editor::CaptureFrame()
	{
		Engine::CaptureFrame(m_screenshotPath);

		m_screenshotPath.clear();

		if (m_screenshotExits)
			Engine::Stop();
	};

	void Editor::RequestScreenshot(const std::string& path, int after_frames, bool exit_after)
	{
		m_screenshotPath = path;
		m_screenshotFrame = after_frames;
		m_screenshotExits = exit_after;
		m_frame = 0;
	};

	void Editor::BuildDefaultLayout(unsigned int dockspace_id)
	{
		ImGui::DockBuilderRemoveNode(dockspace_id);
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

		ImGuiID centre = dockspace_id;
		const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.25f, nullptr, &centre);
		ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.20f, nullptr, &centre);
		const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.25f, nullptr, &centre);
		const ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.40f, nullptr, &left);

		ImGui::DockBuilderDockWindow(EditorPanels::Hierarchy, left);
		ImGui::DockBuilderDockWindow(EditorPanels::Project, left_bottom);
		ImGui::DockBuilderDockWindow(EditorPanels::Scripts, left_bottom);
		ImGui::DockBuilderDockWindow(EditorPanels::TwoD, left_bottom);
		ImGui::DockBuilderDockWindow(EditorPanels::Inspector, right);
		ImGui::DockBuilderDockWindow(EditorPanels::Console, bottom);
		ImGui::DockBuilderDockWindow(EditorPanels::SceneData, bottom);
		ImGui::DockBuilderDockWindow(EditorPanels::Stats, bottom);
		ImGui::DockBuilderDockWindow(EditorPanels::KeyBindings, bottom);

		for (const char* window : centre_windows)
			ImGui::DockBuilderDockWindow(window, centre);

		ImGui::DockBuilderFinish(dockspace_id);
	};

	std::vector<Editor::Panel> Editor::Panels()
	{
		return {
			{ EditorPanels::Hierarchy, &m_showHierarchy, true },
			{ EditorPanels::Project, &m_showProject, true },
			{ EditorPanels::Scripts, &m_showScripts, true },
			{ EditorPanels::TwoD, &m_showTwoD, true },
			{ EditorPanels::Inspector, &m_showInspector, true },
			{ EditorPanels::Scene, &m_showScene, true },
			{ EditorPanels::Game, &m_showGame, true },
			{ EditorPanels::Debug, &m_showDebug, true },
			{ EditorPanels::SceneData, &m_showSceneData, true },
			{ EditorPanels::Console, &m_showConsole, true },
			{ EditorPanels::Shaders, &m_showShaders, false },
			{ EditorPanels::Sprites, &m_showSprites, false },
			{ EditorPanels::Prefab, &m_showPrefab, false },
			{ EditorPanels::Stats, &m_showStats, true },
			{ EditorPanels::KeyBindings, &m_showKeyBindings, true } };
	};

	void Editor::AddPanelSettings()
	{
		static constexpr const char* entry = "Panels";

		ImGuiSettingsHandler handler;
		handler.TypeName = panel_settings;
		handler.TypeHash = ImHashStr(handler.TypeName);
		handler.UserData = this;

		handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) -> void*
		{
			return strcmp(name, entry) == 0 ? handler->UserData : nullptr;
		};

		// One "name=0" or "name=1" line per panel, named by its ID.
		handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* editor, const char* line)
		{
			const char* equals = strrchr(line, '=');

			if (equals == nullptr)
				return;

			const std::string name(line, equals);

			for (const Panel& panel : ((Editor*)editor)->Panels())
				if (name == EditorPanels::IdOf(panel.title))
					*panel.shown = equals[1] == '1';
		};

		handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer)
		{
			buffer->appendf("[%s][%s]\n", handler->TypeName, entry);

			for (const Panel& panel : ((Editor*)handler->UserData)->Panels())
			{
				const std::string_view id = EditorPanels::IdOf(panel.title);

				buffer->appendf("%.*s=%d\n", (int)id.size(), id.data(), *panel.shown ? 1 : 0);
			};

			buffer->append("\n");
		};

		ImGui::AddSettingsHandler(&handler);
	};

	void Editor::ShowSaveLayoutPrompt()
	{
		m_layouts = EditorLayouts::Names(LayoutsFolder());
		snprintf(m_layoutName, sizeof(m_layoutName), "%s", m_currentLayout.c_str());
		m_askForLayoutName = true;
	};

	void Editor::SetCurrentLayout(const std::string& name)
	{
		if (name == m_currentLayout)
			return;

		m_currentLayout = name;
		SaveSettings();
	};

	void Editor::DrawSaveLayoutPrompt()
	{
		if (!m_askForLayoutName)
			return;

		constexpr float name_width = 320.0f;
		constexpr float button_width = 160.0f;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		if (ImGui::Begin(
			"Save Layout",
			nullptr,
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::TextUnformatted("Where every panel sits, and which are open.");
			ImGui::TextDisabled("Letters, digits, spaces, _ and - in the name.");

			ImGui::Separator();

			if (ImGui::IsWindowAppearing())
				ImGui::SetKeyboardFocusHere();

			ImGui::SetNextItemWidth(name_width);

			const bool entered = ImGui::InputText(
				"Name",
				m_layoutName,
				sizeof(m_layoutName),
				ImGuiInputTextFlags_EnterReturnsTrue);

			const bool usable = EditorLayouts::IsValidName(m_layoutName);

			ImGui::TextDisabled(
				"%s",
				!usable
					? "Pick a usable name."
					: std::any_of(
						m_layouts.begin(),
						m_layouts.end(),
						[this](const std::string& layout) { return EditorLayouts::SameName(layout, m_layoutName); })
						? "Replaces the saved layout of that name."
						: "Listed in the Layout menu once saved.");

			ImGui::BeginDisabled(!usable);

			if (ImGui::Button("Save", ImVec2(button_width, 0.0f)) || (entered && usable))
			{
				if (EditorLayouts::Save(LayoutsFolder(), m_layoutName))
				{
					SetCurrentLayout(m_layoutName);
					m_askForLayoutName = false;
				}
				else
					std::cerr << "Could not write the layout '" << m_layoutName << "'" << std::endl;
			};

			ImGui::EndDisabled();

			ImGui::SameLine();

			if (ImGui::Button("Cancel", ImVec2(button_width, 0.0f)) || (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape)))
				m_askForLayoutName = false;
		};

		ImGui::End();
	};

	void Editor::DrawMainMenuBar()
	{
		if (!ImGui::BeginMainMenuBar())
			return;

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem((std::string(EditorIcons::Project) + " New Project...").c_str()))
			{
				snprintf(
					m_newProjectFolder,
					sizeof(m_newProjectFolder),
					"%s",
					DesktopPath().c_str());

				m_askForNewProject = true;
			};

			if (ImGui::MenuItem((std::string(EditorIcons::Project) + " Open Project...").c_str()))
				ShowOpenProjectDialog();

			ImGui::Separator();

			if (ImGui::MenuItem("New Scene"))
				CreateScene("Scene " + std::to_string(++m_sceneCounter));

			if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, m_activeScene != nullptr && !m_playing))
				SaveActiveScene();

			ImGui::Separator();

			if (ImGui::BeginMenu("Open Demo"))
			{
				if (ImGui::IsWindowAppearing())
					m_demos = DemoProjects();

				if (m_demos.empty())
					ImGui::TextDisabled("No demos found.");

				for (const std::filesystem::path& demo : m_demos)
					if (ImGui::MenuItem(demo.filename().string().c_str()))
						OpenDemo(demo.filename().string());

				ImGui::EndMenu();
			};

			ImGui::Separator();

			if (ImGui::MenuItem("Full Refresh", "Ctrl+Shift+R", false, !IsRefreshing()))
				FullRefresh();

			if (ImGui::MenuItem("Exit"))
				Engine::Stop();

			ImGui::EndMenu();
		};

		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, CanStepHistory() && m_history.CanUndo()))
				Undo();

			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, CanStepHistory() && m_history.CanRedo()))
				Redo();

			ImGui::Separator();

			if (ImGui::MenuItem("Cut", "Ctrl+X", false, EditCommands::CanTake(m_selected)))
				Cut();

			if (ImGui::MenuItem("Copy", "Ctrl+C", false, EditCommands::CanTake(m_selected)))
				Copy();

			if (ImGui::MenuItem("Paste", "Ctrl+V"))
				Paste();

			if (ImGui::MenuItem("Delete", "Delete", false, EditCommands::CanTake(m_selected)))
				DeleteSelected();

			ImGui::EndMenu();
		};

		if (ImGui::BeginMenu("GameObject"))
		{
			if (ImGui::MenuItem("Create Empty", nullptr, false, m_activeScene != nullptr))
				Select(m_activeScene->AddChild());

			if (ImGui::MenuItem("Create Child", nullptr, false, m_selected != nullptr))
				Select(m_selected->AddChild());

			ImGui::Separator();

			if (ImGui::MenuItem("Delete", nullptr, false, EditCommands::CanTake(m_selected)))
				DeleteSelected();

			ImGui::EndMenu();
		};

		if (ImGui::BeginMenu("Window"))
		{
			for (const Panel& panel : Panels())
				ImGui::MenuItem(panel.title, nullptr, panel.shown);

			ImGui::Separator();
			ImGui::MenuItem("ImGui Demo", nullptr, &m_showImGuiDemo);

			ImGui::Separator();
			ImGui::MenuItem(EditorPanels::Settings, nullptr, &m_showSettings);

			ImGui::EndMenu();
		};

		const std::string layout_menu = m_currentLayout.empty()
			? std::string("Layout###Layout")
			: "Layout: " + m_currentLayout + "###Layout";

		if (ImGui::BeginMenu(layout_menu.c_str()))
		{
			if (ImGui::IsWindowAppearing())
				m_layouts = EditorLayouts::Names(LayoutsFolder());

			if (m_layouts.empty())
				ImGui::TextDisabled("No saved layouts.");

			for (const std::string& layout : m_layouts)
				if (ImGui::MenuItem(layout.c_str(), nullptr, EditorLayouts::SameName(layout, m_currentLayout)))
					m_pendingLayout = layout;

			ImGui::Separator();

			if (ImGui::MenuItem("Save Layout..."))
				ShowSaveLayoutPrompt();

			if (ImGui::BeginMenu("Delete Layout", !m_layouts.empty()))
			{
				for (const std::string& layout : m_layouts)
					if (ImGui::MenuItem(layout.c_str()))
					{
						if (!EditorLayouts::Delete(LayoutsFolder(), layout))
							std::cerr << "Could not delete the layout '" << layout << "'" << std::endl;
						else if (EditorLayouts::SameName(layout, m_currentLayout))
							SetCurrentLayout("");
					};

				ImGui::EndMenu();
			};

			ImGui::Separator();

			if (ImGui::MenuItem("Reset Layout"))
				m_rebuildLayout = true;

			ImGui::EndMenu();
		};

		// Play control, mirrored in the Game and Debug windows' toolbars.
		ImGui::Separator();

		if (ImGui::MenuItem(Engine::updateScenes ? "Pause" : "Play"))
			TogglePlay();

		if (ImGui::MenuItem("Stop", nullptr, false, IsPlaying()))
			Stop();

		if (ImGui::MenuItem("Play Web", nullptr, false, m_activeScene != nullptr && !m_projectPath.empty()))
			PlayWeb();

		ImGui::EndMainMenuBar();
	};

	void Editor::DrawFolderPrompt()
	{
		if (!m_askForFolder)
			return;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		// Deliberately not a modal: the file browser it opens is one, and two
		// stacked modals is more trouble than the prompt is worth.
		if (ImGui::Begin(
			"Welcome to Loom Editor",
			nullptr,
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::TextUnformatted("The editor works out of a folder of scene files.");
			ImGui::TextDisabled("Everything under it shows up in the Project panel, its textures in the 2D panel and its scripts in the Scripts panel.");

			ImGui::Separator();

			ImGui::SetNextItemWidth(460.0f);
			ImGui::InputText("##folder", m_folderBuffer, sizeof(m_folderBuffer));

			ImGui::SameLine();

			if (ImGui::Button("Browse..."))
				m_dialogs->folder.Open();

			if (ImGui::Button("Open Folder", ImVec2(160.0f, 0.0f)))
				if (OpenProject(m_folderBuffer))
					m_askForFolder = false;

			ImGui::SameLine();

			if (ImGui::Button("Skip", ImVec2(160.0f, 0.0f)))
			{
				if (Scene::GetScenes().empty())
					CreateScene("Untitled Scene");

				m_askForFolder = false;
			};
		};

		ImGui::End();
	};

	bool Editor::OpenDemo(const std::string& name)
	{
		for (const std::filesystem::path& demo : DemoProjects())
			if (demo.filename() == name)
			{
				if (!OpenProject(demo.string()))
					return false;

				m_askForFolder = false;
				return true;
			};

		std::cerr << "No demo called '" << name << "' in "
			<< (std::filesystem::path(ProjectTemplate::EngineRoot()) / "Demos").string()
			<< std::endl;

		return false;
	};

	void Editor::ShowOpenProjectDialog()
	{
		// Projects tend to sit side by side, so the one open is a sibling of
		// whatever is opened next.
		const std::filesystem::path start = m_projectPath.empty()
			? std::filesystem::path(DesktopPath())
			: std::filesystem::path(m_projectPath).parent_path();

		std::error_code code;

		if (std::filesystem::is_directory(start, code))
			m_dialogs->openProject.SetCurrentDirectory(start);

		m_dialogs->openProject.Open();
	};

	void Editor::DrawFileDialogs()
	{
		m_dialogs->folder.Display();

		if (m_dialogs->folder.HasSelected())
		{
			OpenProject(m_dialogs->folder.GetSelected().string());
			m_dialogs->folder.ClearSelected();
			m_askForFolder = false;
		};

		m_dialogs->openProject.Display();

		if (m_dialogs->openProject.HasSelected())
		{
			if (OpenProject(m_dialogs->openProject.GetSelected().string()))
				m_askForFolder = false;

			m_dialogs->openProject.ClearSelected();
		};
	};

	void Editor::DrawHierarchy()
	{
		m_hierarchyFocused = false;

		if (ImGui::Begin(EditorPanels::Hierarchy, &m_showHierarchy))
		{
			m_hierarchyFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

			if (ImGui::Button("New Scene"))
				CreateScene("Scene " + std::to_string(++m_sceneCounter));

			ImGui::SameLine();
			ImGui::TextDisabled("%d scene(s)", (int)Scene::GetScenes().size());

			ImGui::Separator();

			// Iterating a copy: closing a scene from a context menu edits the
			// list the engine hands out.
			const std::vector<Scene*> scenes = Scene::GetScenes();

			ExpandGrownNodes(scenes);

			// The tree ids are GUIDs, which a scene opened twice shares with
			// its copy.
			std::unordered_map<Guid, int> copies{ };

			for (Scene* scene : scenes)
			{
				ImGui::PushID(copies[scene->GetGuid()]++);
				DrawSceneNode(scene);
				ImGui::PopID();
			};

			DrawPrefabNode();
		};

		ImGui::End();
	};

	void Editor::DrawSceneNode(Scene* scene)
	{
		GameObject& root = scene->GetRoot();

		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_AllowOverlap |
			ImGuiTreeNodeFlags_DefaultOpen;

		if (m_selected == &root)
			flags |= ImGuiTreeNodeFlags_Selected;

		const bool is_active = scene == m_activeScene;
		const bool styled = is_active || root.IsDisabled();

		if (styled)
			ImGui::PushStyleColor(
				ImGuiCol_Text,
				root.IsDisabled() ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : EditorTheme::AccentText);

		if (m_expand.contains(root.GetGuid()))
			ImGui::SetNextItemOpen(true);

		const ImRect eye = EyeRect();

		const bool open = ImGui::TreeNodeEx(
			scene->GetGuid().ToString().c_str(),
			flags,
			"%s %s%s",
			EditorIcons::Scene,
			scene->NameAndID().c_str(),
			is_active ? "  [active]" : "");

		if (styled)
			ImGui::PopStyleColor();

		DragSource(*scene);
		TakeImportTarget(&root);
		DragAndDropNode(&root, open);

		if (NodeReleased())
		{
			SetActiveScene(scene);
			Select(&root);
		};

		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Open In Scene View"))
				ShowInSceneView(scene);

			if (ImGui::MenuItem("Add GameObject"))
				Select(scene->AddChild());

			ImGui::Separator();

			if (ImGui::MenuItem("Close Scene", nullptr, false, IsOwned(scene)))
				CloseScene(scene);

			ImGui::EndPopup();
		};

		DrawEnableToggle(&root, false, eye);

		if (!open)
			return;

		const std::vector<GameObject*> children = root.GetChildren();

		for (GameObject* child : children)
			DrawGameObjectNode(child, root.IsDisabled(), false);

		ImGui::TreePop();
	};

	void Editor::DrawGameObjectNode(GameObject* gameObject, bool parent_disabled, bool parent_in_prefab)
	{
		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_AllowOverlap;

		if (gameObject->GetChildren().empty())
			flags |= ImGuiTreeNodeFlags_Leaf;

		if (m_selected == gameObject)
			flags |= ImGuiTreeNodeFlags_Selected;

		if (m_expand.contains(gameObject->GetGuid()))
			ImGui::SetNextItemOpen(true);

		const bool renaming = m_renaming && m_selected == gameObject;
		const bool disabled = parent_disabled || gameObject->IsDisabled();
		const bool instance = Prefabs::LinkOf(*gameObject) != nullptr;
		const bool in_prefab = parent_in_prefab || instance;

		const std::string label = renaming
			? std::string()
			: instance ? std::string(EditorIcons::Prefab) + " " + gameObject->GetName() : gameObject->GetName();

		const ImRect eye = EyeRect();

		if (disabled)
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		else if (in_prefab)
			ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::AccentText);

		const bool open = ImGui::TreeNodeEx(
			gameObject->GetGuid().ToString().c_str(),
			flags,
			"%s",
			label.c_str());

		if (disabled || in_prefab)
			ImGui::PopStyleColor();

		TakeImportTarget(gameObject);
		DragAndDropNode(gameObject, open);

		DragSource(*gameObject);

		// Not again when already selected: the release that ends a double-click
		// would cancel the rename its press just started.
		if (NodeReleased() && m_selected != gameObject)
			Select(gameObject);

		if (ImGui::IsItemHovered() &&
			ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
			!ImGui::IsItemToggledOpen() &&
			!ImGui::IsMouseHoveringRect(eye.Min, eye.Max))
		{
			snprintf(
				m_nameBuffer,
				sizeof(m_nameBuffer),
				"%s",
				gameObject->GetName().c_str());

			m_nameBufferOwner = gameObject;
			m_renaming = true;
			m_focusRename = true;
		};

		if (ImGui::BeginPopupContextItem())
		{
			if (m_selected != gameObject)
				Select(gameObject);

			if (ImGui::MenuItem("Add Child"))
				Select(gameObject->AddChild());

			const std::filesystem::path prefab = PrefabFileOf(*gameObject);
			std::error_code code;

			if (!prefab.empty() && ImGui::MenuItem("Ping Prefab", nullptr, false, std::filesystem::exists(prefab, code)))
				PingAsset(prefab.string());

			if (instance)
			{
				if (ImGui::MenuItem("Unpack Prefab"))
					UnpackPrefab(*gameObject);
			}
			else if (ImGui::MenuItem("Save as Prefab", nullptr, false, !m_projectPath.empty() && gameObject != GetPrefabRoot()))
				SaveAsPrefab(gameObject, "");

			if (ImGui::MenuItem("Delete", nullptr, false, EditCommands::CanTake(m_selected)))
				DeleteSelected();

			ImGui::EndPopup();
		};

		if (renaming)
			DrawRenameField(gameObject);

		DrawEnableToggle(gameObject, parent_disabled, eye);

		if (!open)
			return;

		const std::vector<GameObject*> children = gameObject->GetChildren();

		for (GameObject* child : children)
			DrawGameObjectNode(child, disabled, in_prefab);

		ImGui::TreePop();
	};

	void Editor::DrawEnableToggle(GameObject* gameObject, bool parent_disabled, const ImRect& eye)
	{
		ImGui::SameLine();
		ImGui::SetCursorScreenPos(eye.Min);

		ImGui::PushID(gameObject);

		if (ImGui::InvisibleButton("##enabled", eye.GetSize()))
		{
			gameObject->SetDisabled(!gameObject->IsDisabled());
			m_historyDirty = true;
		};

		ImGui::PopID();

		const bool enabled = !gameObject->IsDisabled();

		ImGui::SetItemTooltip(enabled ? "Disable" : "Enable");

		const ImGuiCol colour = enabled && !ImGui::IsItemHovered()
			? ImGuiCol_TextDisabled
			: ImGuiCol_Text;

		DrawEye(
			ImGui::GetWindowDrawList(),
			eye.GetCenter(),
			eye.GetWidth(),
			enabled,
			ImGui::GetColorU32(colour, parent_disabled ? eye_disabled_by_parent_alpha : 1.0f));
	};

	void Editor::ExpandGrownNodes(const std::vector<Scene*>& scenes)
	{
		std::unordered_map<Guid, size_t> counts{ };
		std::unordered_set<Guid> seen{ };

		m_expand.clear();

		// A node seen for the first time has nothing to compare against, so
		// opening a scene does not unfold everything in it.
		const auto visit =
			[&](const auto& self, const GameObject& node) -> void
			{
				const size_t count = node.GetChildren().size();
				const auto last = m_childCounts.find(node.GetGuid());

				if (last != m_childCounts.end() && count > last->second)
					for (const GameObject* open = &node; open != nullptr; open = open->GetParent())
						m_expand.insert(open->GetGuid());

				counts[node.GetGuid()] = count;

				for (const GameObject* child : node.GetChildren())
					self(self, *child);
			};

		// A copy of a scene opened twice would be compared against the other
		// copy every frame, so only the first is followed.
		for (Scene* scene : scenes)
			if (seen.insert(scene->GetGuid()).second)
				visit(visit, scene->GetRoot());

		m_childCounts = std::move(counts);
	};

	void Editor::DragAndDropNode(GameObject* gameObject, bool open)
	{
		if (!ImGui::BeginDragDropTarget())
			return;

		if (DropAsset(gameObject) || DropScript(gameObject))
		{
			ImGui::EndDragDropTarget();
			return;
		};

		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
			object_payload,
			ImGuiDragDropFlags_AcceptBeforeDelivery |
			ImGuiDragDropFlags_AcceptNoDrawDefaultRect);

		GameObject* dragged = payload
			? LoomObject::GetByGuid<GameObject>(*(const Guid*)payload->Data)
			: nullptr;

		if (dragged == nullptr || !EditCommands::CanMove(*dragged, *gameObject, m_ownedScenes))
		{
			ImGui::EndDragDropTarget();
			return;
		};

		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		const float edge = (max.y - min.y) * sibling_edge;
		const float mouse = ImGui::GetMousePos().y;

		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImU32 colour = ImGui::GetColorU32(ImGuiCol_DragDropTarget);

		const std::vector<GameObject*>& children = gameObject->GetChildren();

		GameObject* parent = gameObject;
		const GameObject* before = nullptr;

		if (gameObject->GetParent() == nullptr || (mouse > min.y + edge && mouse < max.y - edge))
			draw->AddRect(min, max, colour);
		else if (mouse <= min.y + edge)
		{
			draw->AddLine(min, ImVec2(max.x, min.y), colour);

			parent = gameObject->GetParent();
			before = gameObject;
		}
		else
		{
			draw->AddLine(ImVec2(min.x, max.y), max, colour);

			// Below an open node is where its first child is drawn.
			if (open && !children.empty())
				before = children.front();
			else
			{
				parent = gameObject->GetParent();

				const std::vector<GameObject*>& siblings = parent->GetChildren();
				auto next = std::find(siblings.begin(), siblings.end(), gameObject) + 1;

				if (next != siblings.end() && *next == dragged)
					next++;

				before = next == siblings.end() ? nullptr : *next;
			};
		};

		if (payload->IsDelivery())
		{
			// Each scene is seen through its own camera, so an object moved into
			// another scene is followed there rather than vanishing from view.
			Scene* destination = EditCommands::SceneOf(*gameObject);

			if (destination != EditCommands::SceneOf(*dragged))
				ShowInSceneView(destination);

			dragged->SetParent(parent, before);
			Select(dragged);

			m_historyDirty = true;
		};

		ImGui::EndDragDropTarget();
	};

	void Editor::DrawRenameField(GameObject* gameObject)
	{
		ImGui::SameLine();

		// Short of the eye at the end of the row.
		ImGui::SetNextItemWidth(-(ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.x));

		if (m_focusRename)
		{
			ImGui::SetKeyboardFocusHere();
			m_focusRename = false;
		};

		ImGui::InputText(
			"##rename",
			m_nameBuffer,
			sizeof(m_nameBuffer),
			ImGuiInputTextFlags_AutoSelectAll);

		// Enter and clicking away both keep the new name; Escape has already put
		// the old one back in the buffer by the time the field lets go.
		if (!ImGui::IsItemDeactivated())
			return;

		if (m_nameBuffer[0] != '\0')
			gameObject->SetName(m_nameBuffer);

		m_nameBufferOwner = nullptr;
		m_renaming = false;
	};

	bool Editor::NodeReleased()
	{
		if (ImGui::IsItemActivated())
			m_nodePressOpened = ImGui::IsItemToggledOpen();

		return
			ImGui::IsItemDeactivated() &&
			ImGui::IsItemHovered() &&
			!ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left) &&
			!m_nodePressOpened;
	};

	bool Editor::DropAsset(GameObject* gameObject)
	{
		const ImGuiPayload* dragged = ImGui::GetDragDropPayload();

		if (dragged == nullptr || !dragged->IsDataType(ASSET_PAYLOAD))
			return false;

		const std::string path = (const char*)dragged->Data;

		const std::filesystem::path extension = std::filesystem::path(path).extension();

		// A prefab inside itself would take in its own every edit.
		if (extension == Prefab::extension)
		{
			if (!IsInPrefab(gameObject, path) && ImGui::AcceptDragDropPayload(ASSET_PAYLOAD))
				Instantiate(path, *gameObject);

			return true;
		};

		// Anything else dragged out of the Project panel has no place here.
		if (extension != ProjectAssets::shaderExtension)
			return true;

		if (ImGui::AcceptDragDropPayload(ASSET_PAYLOAD))
			DropShader(gameObject, path);

		return true;
	};

	void Editor::DropShader(GameObject* gameObject, const std::string& path)
	{
		Material* material = gameObject->GetComponent<Material>();

		if (material == nullptr)
			material = gameObject->Attach<Material>();

		material->ChangeShader(path);

		Select(gameObject);
		m_historyDirty = true;
	};

	bool Editor::DropScript(GameObject* gameObject)
	{
		const ImGuiPayload* dragged = ImGui::GetDragDropPayload();

		if (dragged == nullptr || !dragged->IsDataType(script_payload))
			return false;

		const std::string name = (const char*)dragged->Data;

		// A state goes in a state machine's Start or Current, not on an object.
		if (const auto entry = m_scriptEntries.find(name); entry != m_scriptEntries.end() && entry->second.state)
			return true;

		if (!ImGui::AcceptDragDropPayload(script_payload))
			return true;

		if (ComponentRegistry::Create(name, *gameObject) == nullptr)
		{
			std::cerr << name << " is not a component the scripts have compiled. One written since the last compile attaches after the next." << std::endl;
			return true;
		};

		Select(gameObject);
		m_historyDirty = true;

		return true;
	};

	void Editor::PollShaders()
	{
		if (ImGui::GetTime() - m_shadersPolled < shader_poll_seconds)
			return;

		m_shadersPolled = ImGui::GetTime();

		Material::ReloadChangedShaders();
	};

	void Editor::ReloadSavedSprites()
	{
		for (const std::string& path : m_sprites.TakeSaved())
			Texture::ReloadShared(path);
	};

	void Editor::DrawInspector()
	{
		if (ImGui::Begin(EditorPanels::Inspector, &m_showInspector))
		{
			if (!m_selectedModel.empty())
				DrawModelPreview();
			else if (m_selected == nullptr)
				ImGui::TextDisabled("Nothing selected.");
			else
			{
				GameObject* gameObject = m_selected;

				// The name box is only refilled when the selection changes or a
				// rename ends, so typing into it is not fighting the object's
				// current name.
				if (m_nameBufferOwner != gameObject)
				{
					snprintf(
						m_nameBuffer,
						sizeof(m_nameBuffer),
						"%s",
						gameObject->GetName().c_str());

					m_nameBufferOwner = gameObject;
				};

				ImGui::TextDisabled("%s", gameObject->GetGuid().ToString().c_str());

				if (ImGui::IsItemClicked())
					ImGui::SetClipboardText(gameObject->GetGuid().ToString().c_str());

				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Click to copy");

				constexpr const char* name_label = "Name";

				const float column = LabelColumn(
					FieldNames::Of(*gameObject),
					ImGui::CalcTextSize(name_label).x);

				Label(name_label, column);
				ImGui::PushItemWidth(-FLT_MIN);

				if (ImGui::InputText(
					"##name",
					m_nameBuffer,
					sizeof(m_nameBuffer),
					ImGuiInputTextFlags_EnterReturnsTrue))
					gameObject->SetName(m_nameBuffer);

				ImGui::PopItemWidth();

				DrawTransform(gameObject, column);
				DrawThread(gameObject, column);

				ImGui::Separator();

				const std::vector<ComponentBase*> components = gameObject->GetComponents();

				for (ComponentBase* component : components)
					DrawComponent(gameObject, component);

				if (!components.empty())
					ImGui::Separator();

				if (ImGui::Button("Add Component", ImVec2(-FLT_MIN, 0.0f)))
					ImGui::OpenPopup("##add_component");

				// Not while the Inspector is appearing: docking it closes a popup
				// opened then. With no click to open it at, it opens under the
				// button.
				if (m_openAddComponent && !ImGui::IsWindowAppearing())
				{
					ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
					ImGui::OpenPopup("##add_component");
					m_openAddComponent = false;
				};

				if (ImGui::BeginPopup("##add_component"))
				{
					if (ComponentRegistry::All().empty())
						ImGui::TextDisabled("Nothing registered.");

					DrawComponentMenu(gameObject, m_componentMenu.Build(m_scripts.GetTypes()));

					ImGui::EndPopup();
				};
			};
		};

		ImGui::End();
	};

	void Editor::DrawComponentMenu(GameObject* gameObject, const ComponentMenu::Node& node)
	{
		for (const ComponentMenu::Node& child : node.children)
			if (ImGui::BeginMenu(child.name.c_str()))
			{
				DrawComponentMenu(gameObject, child);
				ImGui::EndMenu();
			};

		if (!node.children.empty() && !node.components.empty())
			ImGui::Separator();

		for (const std::string& component : node.components)
		{
			if (ImGui::MenuItem(component.c_str()))
				ComponentRegistry::Create(component, *gameObject);

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("Right-click to change its category");

			DrawCategoryPopup(component);
		};
	};

	void Editor::DrawCategoryPopup(const std::string& component)
	{
		constexpr float field_width = 240.0f;

		if (!ImGui::BeginPopupContextItem())
			return;

		if (ImGui::IsWindowAppearing())
		{
			const std::string category = m_componentMenu.CategoryOf(
				component,
				std::ranges::find(m_scripts.GetTypes(), component) != m_scripts.GetTypes().end());

			snprintf(m_categoryBuffer, sizeof(m_categoryBuffer), "%s", category.c_str());
			ImGui::SetKeyboardFocusHere();
		};

		ImGui::SetNextItemWidth(field_width);

		if (ImGui::InputTextWithHint(
			"##category",
			"Category, '/' between nested ones",
			m_categoryBuffer,
			sizeof(m_categoryBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue))
		{
			SetComponentCategory(component, m_categoryBuffer);
			ImGui::CloseCurrentPopup();
		};

		if (ImGui::MenuItem("Use Its Own Category", nullptr, false, m_componentMenu.IsFiled(component)))
			SetComponentCategory(component, "");

		if (m_projectFile.empty())
			ImGui::TextDisabled("No project is open to keep this in.");

		ImGui::EndPopup();
	};

	void Editor::SetComponentCategory(const std::string& component, const std::string& category)
	{
		m_componentMenu.Set(component, category);

		if (!m_projectFile.empty() && !m_componentMenu.Save(m_projectFile))
			std::cerr << "Could not write the component categories to " << m_projectFile << std::endl;
	};

	void Editor::DrawModelPreview()
	{
		constexpr float max_preview_side = 512.0f;
		constexpr float turn_radians_per_second = 0.6f;

		ImGui::TextUnformatted(std::filesystem::path(m_selectedModel).filename().string().c_str());
		ImGui::TextDisabled("%s", m_selectedModel.c_str());
		ImGui::Separator();

		m_modelPreview.Show(m_selectedModel);

		if (!m_modelPreview.IsLoaded())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("Could not read this model. The Console has the reason.");
			ImGui::PopStyleColor();
			return;
		};

		const int side = (int)(std::min)(ImGui::GetContentRegionAvail().x, max_preview_side);

		if (side >= 1)
		{
			const ImVec4& background = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);

			m_modelPreview.Render(side, (float)ImGui::GetTime() * turn_radians_per_second, &background.x);

			const EditorViewport& target = m_modelPreview.GetTarget();

			if (target.GetTexture())
				ImGui::Image(
					target.GetTextureID(),
					ImVec2((float)target.GetWidth(), (float)target.GetHeight()),
					ImVec2(0.0f, 1.0f),
					ImVec2(1.0f, 0.0f));
		};

		ImGui::Text(
			"%zu mesh(es), %zu triangle(s)",
			m_modelPreview.GetMeshCount(),
			m_modelPreview.GetTriangleCount());

		if (ImGui::Button("Import", ImVec2(-FLT_MIN, 0.0f)))
			QueueImport(m_selectedModel);

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Into the active scene");
	};

	void Editor::DrawTransform(GameObject* gameObject, float column)
	{
		if (!ImGui::CollapsingHeader("Transform", header_flags))
			return;

		Transform& transform = gameObject->transform;

		const auto row =
			[column](const char* name, glm::vec3& value, float speed)
			{
				ImGui::PushID(name);
				Label(name, column);
				AxisGui::DragFloatN("##value", glm::value_ptr(value), 3, speed, float_format);
				ImGui::PopID();
			};

		ImGui::PushItemWidth(-FLT_MIN);

		row("Position", *transform.position, drag_speed);
		row("Rotation", *transform.rotation, rotation_drag_speed);
		row("Scale", *transform.scale, drag_speed);

		ImGui::PopItemWidth();
	};

	void Editor::DrawThread(GameObject* gameObject, float column)
	{
		if (!ImGui::CollapsingHeader("Thread", header_flags))
			return;

		ImGui::PushItemWidth(-FLT_MIN);

		bool inherit = gameObject->InheritsThreadID();

		Label("Inherit Parent", column);

		if (ImGui::Checkbox("##inherit", &inherit))
			gameObject->SetInheritThreadID(inherit);

		// An inheriting object follows its parent, so its own ID is not the
		// inspector's to set.
		ImGui::BeginDisabled(inherit);

		int thread = gameObject->GetThreadID();

		Label("Thread ID", column);

		if (ImGui::SliderInt(
			"##thread",
			&thread,
			unprocessed_thread,
			(int)std::thread::hardware_concurrency()))
			gameObject->SetThreadID(thread);

		ImGui::EndDisabled();

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%d is not processed", unprocessed_thread);

		ImGui::PopItemWidth();
	};

	void Editor::DrawComponent(GameObject* gameObject, ComponentBase* component)
	{
		ImGui::PushID(component);

		const std::string name = ComponentRegistry::NameOf(*component);

		const bool open = ImGui::CollapsingHeader(name.c_str(), header_flags);

		// The remove button rides on the header's own line, Unity-style.
		ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
		if (ImGui::SmallButton("x"))
			gameObject->DetachComponent(component);

		if (!open)
		{
			ImGui::PopID();
			return;
		};

		// The Serial members the component declared, then whatever
		// else it wants to draw for itself.
		const std::vector<std::string> labels = FieldNames::Of(*component);

		DrawFields(*component, labels, FieldLabelColumn(*component, labels));

		if (StateMachineBase* machine = dynamic_cast<StateMachineBase*>(component))
		{
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float half = (ImGui::GetContentRegionAvail().x - spacing) / 2.0f;

			// A script goes in a project's scripts project, which needs a project.
			ImGui::BeginDisabled(m_projectPath.empty());

			if (ImGui::Button("New State", ImVec2(half, 0.0f)))
				AskForAsset(AssetKind::State, m_projectPath);

			ImGui::SameLine(0.0f, spacing);

			if (ImGui::Button("New State Machine", ImVec2(half, 0.0f)))
				AskForAsset(AssetKind::StateMachine, m_projectPath);

			ImGui::EndDisabled();

			machine->DrawMachineGui();
		};

		component->OnGui();

		ImGui::PopID();
	};

	void Editor::DrawFields(LoomObject& object, const std::vector<std::string>& labels, float column)
	{
		ImGui::PushItemWidth(-FLT_MIN);

		const std::vector<const std::vector<FieldNames::Enumerator>*> enumerators = FieldNames::EnumeratorsOf(object);

		// Where the fields outside any category start, so those nested deeper
		// give their labels the indent and keep their values in one column.
		const float left = ImGui::GetCursorPosX();

		const auto draw_field = [&](size_t i)
		{
			const SerializedField& field = object.GetFields()[i];

			if (field.type == FieldType::Uniforms)
				return;

			ImGui::PushID(&field);

			Label(labels[i], (std::max)(column - (ImGui::GetCursorPosX() - left), 0.0f));

			const char* label = "##value";
			bool changed = false;

			const std::vector<std::string> choices = object.GetFieldChoices(field);

			if (!choices.empty())
				changed = DrawChoiceField(label, field, choices);
			else if (enumerators[i])
				changed = DrawEnumField(label, field, *enumerators[i]);
			else switch (field.type)
			{
			case FieldType::Bool:
				changed = ImGui::Checkbox(label, (bool*)field.data);
				break;

			case FieldType::Int:
				changed = ImGui::DragScalar(label, ImGuiDataType_S32, field.data);
				break;

			case FieldType::UInt:
				changed = ImGui::DragScalar(label, ImGuiDataType_U32, field.data);
				break;

			case FieldType::Int64:
				changed = ImGui::DragScalar(label, ImGuiDataType_S64, field.data);
				break;

			case FieldType::UInt64:
				changed = ImGui::DragScalar(label, ImGuiDataType_U64, field.data);
				break;

			case FieldType::Float:
				changed = ImGui::DragFloat(label, (float*)field.data, drag_speed, 0.0f, 0.0f, float_format);
				break;

			case FieldType::Double:
				changed = ImGui::DragScalar(label, ImGuiDataType_Double, field.data, drag_speed, nullptr, nullptr, double_format);
				break;

			case FieldType::String:
			{
				std::string& text = *(std::string*)field.data;

				// One name a line, such as a skin's joints: worth seeing the
				// length of, not worth one box that cannot show its lines.
				if (text.find('\n') != std::string::npos)
				{
					ImGui::Text("%d lines", (int)std::count(text.begin(), text.end(), '\n'));
					break;
				};

				changed = InputTextString(label, text);
				changed |= AcceptAssetDrop(text);
			}
			break;

			case FieldType::Vec2:
				changed = AxisGui::DragFloatN(label, (float*)field.data, 2, drag_speed, float_format);
				break;

			case FieldType::Vec3:
				changed = AxisGui::DragFloatN(label, (float*)field.data, 3, drag_speed, float_format);
				break;

			case FieldType::Vec4:
				changed = AxisGui::DragFloatN(label, (float*)field.data, 4, drag_speed, float_format);
				break;

			case FieldType::FloatArray:
			{
				// Vertex data: worth seeing the size of, not worth 3000 drag boxes.
				const std::vector<float>& values = *(std::vector<float>*)field.data;

				ImGui::Text("%d floats", (int)values.size());
			}
			break;

			case FieldType::Reference:
				changed = DrawReferenceField(field);
				break;

			case FieldType::State:
				changed = StateMachineMonitor::DrawStateField(*(StateReference*)field.data);
				break;

			case FieldType::Uniforms:
				break;
			};

			if (changed)
			{
				field.Changed();
				object.OnFieldChanged(field);
			};

			ImGui::PopID();
		};

		// A Button fills the row, pressed as a button anywhere else is.
		const auto draw_item = [&](const FieldItem& item)
		{
			if (item.kind == FieldItem::Field)
			{
				draw_field(item.index);
				return;
			};

			const LoomObject::FieldButton& button = object.GetButtons()[item.index];

			ImGui::PushID(&button);

			if (ImGui::Button(std::string(NameDisplay(button.name)).c_str(), ImVec2(-FLT_MIN, 0.0f)) && button.press && *button.press)
				(*button.press)();

			ImGui::PopID();
		};

		DrawFieldGroup(GroupFields(object), draw_item);

		ImGui::PopItemWidth();
	};

	void Editor::DrawScene()
	{
		const std::string title =
			std::string(EditorIcons::Scene) + " " +
			(m_activeScene ? m_activeScene->GetName() : std::string("Scene")) + EditorPanels::SceneId;

		if (m_sceneFocusFrames > 0)
		{
			m_sceneFocusFrames--;
			ImGui::SetNextWindowFocus();
		};

		if (ImGui::Begin(title.c_str(), &m_showScene, view_flags))
		{
			Watch(View::Scene);

			DrawSceneView();
		};

		ImGui::End();
	};

	void Editor::DrawSceneView()
	{
		if (m_activeScene == nullptr)
			ImGui::TextDisabled("No scene open. Pick one in the Project panel.");
		else
		{
			DrawView(m_sceneView, { m_activeScene }, &m_sceneCamera, &m_sceneOutline);
			TakeImportTarget(&m_activeScene->GetRoot());
		};
	};

	void Editor::DrawPrefab()
	{
		if (!m_showPrefab)
		{
			ClosePrefab();
			return;
		};

		const std::string title =
			std::string(EditorIcons::Prefab) + " " +
			(m_prefabScene ? m_prefabScene->GetName() : std::string("Prefab")) + EditorPanels::PrefabId;

		if (m_prefabFocusFrames > 0)
		{
			m_prefabFocusFrames--;
			ImGui::SetNextWindowFocus();
		};

		if (ImGui::Begin(title.c_str(), &m_showPrefab, view_flags))
		{
			Watch(View::Prefab);

			DrawPrefabView();
		};

		ImGui::End();
	};

	void Editor::DrawPrefabView()
	{
		if (GetPrefabRoot() == nullptr)
			ImGui::TextDisabled("No prefab open. Double-click one in the Project panel.");
		else DrawView(m_prefabView, { m_prefabScene }, &m_prefabCamera, &m_prefabOutline);
	};

	void Editor::DrawPrefabNode()
	{
		GameObject* root = GetPrefabRoot();

		if (root == nullptr)
			return;

		ImGui::Separator();
		ImGui::TextDisabled("%s %s", EditorIcons::Prefab, m_prefabPath.c_str());

		DrawGameObjectNode(root, false, false);
	};

	void Editor::DrawGame()
	{
		// While it plays, the keys are the game's, not a way round the panel's buttons.
		if (ImGui::Begin(EditorPanels::Game, &m_showGame, m_playing ? view_flags | ImGuiWindowFlags_NoNavInputs : view_flags))
		{
			Watch(View::Game);

			DrawToolbar();
			DrawGameView();
		};

		ImGui::End();
	};

	void Editor::DrawGameView()
	{
		m_gamePanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		m_gamePanelHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

		if (Scene::GetScenes().empty())
			ImGui::TextDisabled("No scene open. Pick one in the Project panel.");
		else
		{
			// GLFW reports the cursor from the main window's corner, which
			// ImGui's screen space only starts at without multi-viewports.
			const ImVec2 origin = ImGui::GetMainViewport()->Pos;
			const ImVec2 corner = ImGui::GetCursorScreenPos();
			const ImVec2 available = ImGui::GetContentRegionAvail();

			Input::SetView((int)(corner.x - origin.x), (int)(corner.y - origin.y), (int)available.x, (int)available.y);
			DrawView(m_gameView, Scene::GetScenes(), nullptr);

			if (m_activeScene)
				TakeImportTarget(&m_activeScene->GetRoot());
		};
	};

	void Editor::DrawDebug()
	{
		if (ImGui::Begin(EditorPanels::Debug, &m_showDebug, view_flags))
		{
			Watch(View::Debug);

			DrawToolbar();
			DrawDebugView();
		};

		ImGui::End();
	};

	void Editor::DrawDebugView()
	{
		if (Scene::GetScenes().empty())
			ImGui::TextDisabled("Nothing loaded.");
		else
		{
			DrawView(m_debugView, Scene::GetScenes(), &m_debugCamera, &m_debugOutline);

			if (m_activeScene)
				TakeImportTarget(&m_activeScene->GetRoot());
		};
	};

	void Editor::DrawToolbar()
	{
		constexpr const char* play_label = "Play";
		constexpr const char* pause_label = "Pause";
		constexpr const char* stop_label = "Stop";
		constexpr const char* step_label = "Step";
		constexpr const char* play_labels[] = { play_label, pause_label, stop_label, step_label };
		constexpr float frame_sides = 2.0f;

		float label_width = 0.0f;

		for (const char* label : play_labels)
			label_width = (std::max)(label_width, ImGui::CalcTextSize(label).x);

		const ImVec2 play_button_size(label_width + ImGui::GetStyle().FramePadding.x * frame_sides, 0.0f);

		ImGui::BeginDisabled(Engine::updateScenes || m_playAfterCompile);

		if (ImGui::Button(play_label, play_button_size))
			TogglePlay();

		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!Engine::updateScenes);

		if (ImGui::Button(pause_label, play_button_size))
			TogglePlay();

		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!IsPlaying());

		if (ImGui::Button(stop_label, play_button_size))
			Stop();

		ImGui::EndDisabled();
		ImGui::SameLine();

		// A build has the scenes down, so there is nothing to step, and a
		// running scene has no single frame to step to.
		ImGui::BeginDisabled(m_scripts.IsBuilding() || Engine::updateScenes);

		if (ImGui::Button(step_label, play_button_size))
		{
			BeginPlay();
			m_steppedLastFrame = true;
		};

		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::ColorEdit4(
			"Background",
			Engine::clearColor,
			ImGuiColorEditFlags_NoInputs);

		ImGui::Separator();
	};

	void Editor::DrawView(EditorViewport& view, const std::vector<Scene*>& scenes, EditorCamera* camera, SelectionOutline* outline)
	{
		if (camera)
		{
			ImGui::TextDisabled("(?)");
			ImGui::SetItemTooltip("%s", camera_controls);
		};

		const ImVec2 available = ImGui::GetContentRegionAvail();

		if (available.x < 1.0f || available.y < 1.0f)
			return;

		// The camera is steered before the scenes are drawn, so the image keeps
		// up with the mouse rather than trailing it by a frame.
		if (camera)
		{
			const ImVec2 corner = ImGui::GetCursorScreenPos();

			ImGui::InvisibleButton(
				"##fly",
				available,
				ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

			camera->Drive(ImGui::IsItemHovered(), ImGui::IsItemActive());

			if (ImGui::IsItemActive() || ImGui::IsItemDeactivated())
				m_flyingView = true;

			ImGui::SetCursorScreenPos(corner);
		};

		Camera* seen_through = camera ? camera->Get() : nullptr;

		const bool ringed = outline && seen_through && m_selected && std::any_of(
			scenes.begin(),
			scenes.end(),
			[this](const Scene* scene) { return IsInHierarchy(scene->GetRoot(), m_selected); });

		// Rendering here is fine mid-frame: ImGui only records the texture id
		// now and draws with it once the frame is submitted.
		view.Render(
			(int)available.x,
			(int)available.y,
			Engine::clearColor,
			[&]()
			{
				// Without an editor camera this is the Game view, which shows
				// the UI over every scene in it.
				if (!camera)
					Ui::BeginOverlay();

				for (Scene* scene : scenes)
					scene->Render(seen_through, scenes);

				if (!camera)
					Ui::DrawOverlay();

				if (ringed)
				{
					outline->Draw(*m_selected, *seen_through);
					m_colliderGizmos.Draw(*m_selected, *seen_through);
				};
			});

		if (view.GetTexture())
			ImGui::Image(
				view.GetTextureID(),
				ImVec2((float)view.GetWidth(), (float)view.GetHeight()),
				ImVec2(0.0f, 1.0f),	// The framebuffer is bottom-up,
				ImVec2(1.0f, 0.0f));// so the V axis is flipped
	};

	void Editor::QueueImport(const std::string& path)
	{
		m_importQueue.push_back(QueuedImport{ path });
	};

	void Editor::QueueHistoryStep(bool undo)
	{
		m_historySteps.push_back(undo);
	};

	void Editor::Drop(const std::string& path, float x, float y)
	{
		m_importQueue.push_back(QueuedImport{ path, true, x, y });
	};

	void Editor::TakeImportTarget(GameObject* gameObject)
	{
		if (!m_importQueue.empty() && ImGui::IsItemHovered())
			m_importTarget = gameObject;
	};

	void Editor::ImportQueued()
	{
		if (m_importQueue.empty())
			return;

		// A build has the scenes down; the files wait for them to come back and
		// be drawn, so there is something under the mouse to import into.
		if (!m_scripts.IsIdle())
		{
			m_importTarget = nullptr;
			m_importFolder.clear();
			m_importPlaced = false;
			return;
		};

		const QueuedImport& front = m_importQueue.front();

		// The mouse event lands at the start of the next frame, and the panels
		// drawn in it pick the target.
		if (front.positioned && !m_importPlaced)
		{
			ImGui::GetIO().AddMousePosEvent(front.x, front.y);
			m_importPlaced = true;
			m_importTarget = nullptr;
			m_importFolder.clear();
			return;
		};

		// Files dropped together go in together; a drop somewhere else waits
		// for the mouse to be put there.
		const auto together =
			[&front](const QueuedImport& queued)
			{
				return queued.positioned == front.positioned && queued.x == front.x && queued.y == front.y;
			};

		const auto end = std::find_if_not(m_importQueue.begin(), m_importQueue.end(), together);

		if (!m_importFolder.empty())
		{
			for (auto queued = m_importQueue.begin(); queued != end; ++queued)
				CopyIntoProject(queued->path);

			RefreshProjectAssets();
		}
		else
		{
			GameObject* parent = m_importTarget ? m_importTarget : m_selected;

			if (parent == nullptr)
				parent = m_activeScene
					? &m_activeScene->GetRoot()
					: &CreateScene("Scene " + std::to_string(++m_sceneCounter))->GetRoot();

			for (auto queued = m_importQueue.begin(); queued != end; ++queued)
				if (!IsModelFile(queued->path))
					std::cerr << "Import: " << queued->path << " is not a model format Assimp reads" << std::endl;
				else if (GameObject* model = ImportModel(queued->path, *parent))
				{
					Select(model);
					m_historyDirty = true;
				};
		};

		m_importQueue.erase(m_importQueue.begin(), end);
		m_importTarget = nullptr;
		m_importFolder.clear();
		m_importPlaced = false;
	};

	void Editor::QueueClick(float x, float y, float to_x, float to_y)
	{
		m_clicks.push_back(QueuedClick{ x, y, to_x, to_y });
	};

	void Editor::ClickQueued()
	{
		if (m_clicks.empty() || !m_scripts.IsIdle())
			return;

		enum { Move, Press, Drag, Release };

		QueuedClick& click = m_clicks.front();
		ImGuiIO& io = ImGui::GetIO();

		switch (click.step++)
		{
		case Move:		io.AddMousePosEvent(click.x, click.y);						break;
		case Press:		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);		break;
		case Drag:		io.AddMousePosEvent(click.toX, click.toY);					break;
		case Release:
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			m_clicks.erase(m_clicks.begin());
			break;
		};
	};

	void Editor::TakeImportFolder(const std::string& folder, bool hovered)
	{
		if (hovered && m_importPlaced && m_importFolder.empty())
			m_importFolder = folder;
	};

	void Editor::CopyIntoProject(const std::string& file)
	{
		if (!ProjectAssets::IsGameFile(file) && !IsModelFile(file))
		{
			std::cerr << "Project: " << file << " is not a file the Project panel lists" << std::endl;
			return;
		};

		std::string error;
		const std::string copied = ProjectAssets::CopyInto(file, m_importFolder, &error);

		if (copied.empty())
		{
			std::cerr << "Project: " << error << std::endl;
			return;
		};

		std::cout << "Copied " << file << " to " << copied << std::endl;

		if (ProjectAssets::IsTextureFile(copied))
			return RevealTwoD(copied);

		m_selectedAsset = std::filesystem::path(copied).lexically_normal().string();
		m_revealSelectedAsset = true;
	};

	void Editor::DrawSceneData()
	{
		if (ImGui::Begin(EditorPanels::SceneData, &m_showSceneData))
		{
			if (m_activeScene == nullptr)
				ImGui::TextDisabled("No scene open.");
			else
			{
				// Rebuilt every frame on purpose: this is the scene as it would
				// be written right now, edits included.
				const std::string text = SceneSerializer::Serialize(*m_activeScene);

				if (ImGui::Button("Copy"))
					ImGui::SetClipboardText(text.c_str());

				ImGui::SameLine();
				ImGui::TextDisabled("%d lines", (int)std::count(text.begin(), text.end(), '\n'));

				ImGui::Separator();

				if (ImGui::BeginChild(
					"##text",
					ImVec2(0.0f, 0.0f),
					ImGuiChildFlags_None,
					ImGuiWindowFlags_HorizontalScrollbar))
					ImGui::TextUnformatted(text.c_str());

				ImGui::EndChild();
			};
		};

		ImGui::End();
	};

	void Editor::DrawStats()
	{
		if (ImGui::Begin(EditorPanels::Stats, &m_showStats))
		{
			const ImGuiIO& io = ImGui::GetIO();

			constexpr float milliseconds_per_second = 1000.0f;

			// ImGui already averages Framerate over its last 60 frames.
			ImGui::Text(
				"%d FPS (%d ms/frame)",
				(int)std::ceil(io.Framerate),
				(int)std::ceil(milliseconds_per_second / io.Framerate));
			ImGui::Text("Graphics API: %s", EditorSettings::BackendLabel(Engine::backend));
			ImGui::Text("Job threads: %u of %u", Jobs::Threads(), Jobs::Cores());

			ImGui::Text("Scenes: %d", (int)Scene::GetScenes().size());
			ImGui::Text("GameObjects: %d", (int)GameObject::GetObjectCount());
			ImGui::Text("Registered components: %d", (int)ComponentRegistry::All().size());

			const char* const updating =
				Engine::updateScenes
					? "yes"
					: "paused";

			ImGui::Text("Updating: %s", IsPlaying() ? updating : "stopped");
			ImGui::Text("Game view: %d x %d", m_gameView.GetWidth(), m_gameView.GetHeight());

			if (m_activeScene)
			{
				ImGui::Text("Active scene: %s", m_activeScene->NameAndID().c_str());
				ImGui::TextDisabled("%s", m_activeScene->GetGuid().ToString().c_str());
			}
			else ImGui::TextDisabled("No active scene");
		};

		ImGui::End();
	};
};
