#include "Editor.h"

#include "EditorLog.h"
#include "EditorTheme.h"
#include "ExtensionRegistry.h"

#include "Audio.h"
#include "Component.h"
#include "ComponentRegistry.h"
#include "ComputeShader.h"
#include "EditorGui.h"
#include "Engine.h"
#include "Executable.h"
#include "GameObject.h"
#include "Input.h"
#include "Jobs.h"
#include "Light.h"
#include "LoomObject.h"
#include "Material.h"
#include "PixelShader.h"
#include "MissingComponent.h"
#include "Prefab.h"
#include "PreVertex.h"
#include "Profiler.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "Sprite.h"
#include "StageTiming.h"
#include "Textures.h"
#include "Transform.h"
#include "Ui.h"

#include "Utilities/AxisGui.h"
#include "Utilities/DialogueManager.h"
#include "Utilities/StateMachine.h"
#include "Utilities/StateMachineMonitor.h"

#include "ComponentDrag.h"
#include "Cutscenes.h"
#include "EditCommands.h"
#include "EditorFileDialogs.h"
#include "EditorIcons.h"
#include "FieldNames.h"
#include "Hierarchy.h"
#include "ModelImporter.h"
#include "MultiEdit.h"
#include "ProjectAssets.h"
#include "ProjectTemplate.h"
#include "RenderingObjects.h"
#include "Shapes.h"
#include "SpriteAsset.h"
#include "TextFit.h"
#include "UiScript.h"

#include "OpenGL.h"

#include "glm/gtc/type_ptr.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#else
#include <cstdlib>
#endif


namespace Loom
{
	namespace
	{
		#pragma region Inspector

		constexpr float rotation_drag_speed = 0.5f;

		constexpr int selection_frames = 60;

		constexpr ImGuiTreeNodeFlags header_flags =
			ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap;

		// A category of fields, folded until opened.
		constexpr ImGuiTreeNodeFlags field_group_flags = ImGuiTreeNodeFlags_SpanAvailWidth;

		#pragma endregion

		#pragma region Window Buttons

		// How much deeper the menu bar is than a frame, for room to drag the window by.
		constexpr float menu_bar_padding = 1.375f;

		constexpr int window_button_count = 3;
		constexpr float window_button_aspect = 1.6f;
		constexpr float window_glyph_size = 0.2f;
		constexpr float window_glyph_offset = 0.5f;
		constexpr ImU32 close_hovered = IM_COL32(196, 43, 28, 255);
		constexpr ImU32 close_pressed = IM_COL32(148, 32, 21, 255);
		constexpr ImU32 close_glyph = IM_COL32(255, 255, 255, 255);

		ImGuiMouseCursor ResizeCursor(WindowFrame::Hit hit)
		{
			switch (hit)
			{
			case WindowFrame::Hit::Left:
			case WindowFrame::Hit::Right: return ImGuiMouseCursor_ResizeEW;
			case WindowFrame::Hit::Top:
			case WindowFrame::Hit::Bottom: return ImGuiMouseCursor_ResizeNS;
			case WindowFrame::Hit::TopLeft:
			case WindowFrame::Hit::BottomRight: return ImGuiMouseCursor_ResizeNWSE;
			case WindowFrame::Hit::TopRight:
			case WindowFrame::Hit::BottomLeft: return ImGuiMouseCursor_ResizeNESW;
			default: return ImGuiMouseCursor_None;
			};
		};

		#pragma endregion

		#pragma region Field Groups

		// A field or Button by index, or a group of them by index in its parent.
		struct FieldItem
		{
			enum Kind { Field, Button, Group };

			Kind kind = Field;
			size_t index = 0;
		};

		// Fields and Buttons by category, nested at each '/', in declaration order.
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

		#pragma endregion

		#pragma region Utilities

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

		#pragma endregion

		#pragma region Paths

		// Beside the executable, since opening a project moves the working directory.
		std::filesystem::path BesideExecutable(const char* file)
		{
			return ExecutablePath().parent_path() / file;
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
			std::filesystem::path desktop;
			std::error_code code;

#ifdef _WIN32
			PWSTR wide = nullptr;

			if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &wide)))
				desktop = wide;

			CoTaskMemFree(wide);
#else
			if (const char* home = std::getenv("HOME"))
			{
				desktop = std::filesystem::path(home) / "Desktop";

				if (!std::filesystem::is_directory(desktop, code))
					desktop = home;
			};
#endif

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

		// The nearest folder above file with a project file, or the file's own folder.
		std::filesystem::path ProjectFolderOf(const std::filesystem::path& file)
		{
			std::filesystem::path folder = file.parent_path();

			while (!HasProjectFile(folder))
			{
				if (folder == folder.parent_path())
					return file.parent_path();

				folder = folder.parent_path();
			};

			return folder;
		};

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

		#pragma endregion

		#pragma region Timing

		// A window docked on its first frame only takes its tab on the next.
		constexpr int prefab_focus_frames = 2;

		// How often the editor checks whether a shader file was edited outside it.
		constexpr double shader_poll_seconds = 0.5;

		#pragma endregion

		#pragma region Hierarchy Eye

		// The Hierarchy eye against the font size: half-width, lid bow, pupil and lashes.
		constexpr float eye_half_width = 0.42f;
		constexpr float eye_lid_bow = 0.5f;
		constexpr float eye_pupil = 0.16f;
		constexpr float eye_lash = 0.14f;
		constexpr float eye_stroke = 1.5f;
		constexpr float eye_disabled_by_parent_alpha = 0.4f;

		// Where each lash leaves the lower lid, as a fraction of its width.
		constexpr float eye_lash_positions[] = { -0.5f, 0.0f, 0.5f };

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

		#pragma endregion

		#pragma region Window Callbacks

		// GLFW's callbacks carry no user data, and there is one editor.
		Editor* window_editor = nullptr;

		void OnClose(GLFWwindow* window)
		{
			glfwSetWindowShouldClose(window, GLFW_FALSE);
			window_editor->Quit();
		};

		// Only from the event poll, the one place a frame may run inside another.
		void RedrawWhileHeld()
		{
			if (Engine::IsPolling())
				Engine::renderFrame();
		};

		void OnDrop(GLFWwindow* window, int count, const char** paths)
		{
			// The OS swallows mouse moves while dragging; with viewports, ImGui works in screen space.
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
				window_editor->Drop(paths[i], (float)x, (float)y);
		};

		std::unordered_map<unsigned int, std::unique_ptr<WindowFrame>>* torn_frames = nullptr;

		// The platform's, run once the window's frame is off it.
		void (*destroy_platform_window)(ImGuiViewport*) = nullptr;

		void DestroyPlatformWindow(ImGuiViewport* viewport)
		{
			if (torn_frames != nullptr)
				torn_frames->erase(viewport->ID);

			destroy_platform_window(viewport);
		};

		// On every OS window, since a panel dragged out takes drops in its own.
		void SetDropCallback(GLFWdropfun callback)
		{
			for (ImGuiViewport* viewport : ImGui::GetPlatformIO().Viewports)
				if (viewport->PlatformHandle)
					glfwSetDropCallback((GLFWwindow*)viewport->PlatformHandle, callback);
		};

		#pragma endregion

		#pragma region Panels

		constexpr const char* centre_windows[] = { EditorPanels::Game, EditorPanels::Debug, EditorPanels::PrefabId, EditorPanels::Shaders, EditorPanels::Dialogues, EditorPanels::Sprites, EditorPanels::Model };

		// What lights the Prefab panel, which the Hierarchy leaves out.
		constexpr const char* prefab_light = "Prefab Light";

		// Panels docked as a tab beside another, each with the one it goes beside.
		constexpr std::pair<const char*, const char*> side_windows[] = { { EditorPanels::KeyBindings, EditorPanels::Console }, { EditorPanels::BuildSettings, EditorPanels::Console }, { EditorPanels::Tests, EditorPanels::Console }, { EditorPanels::Palette, EditorPanels::Inspector } };

		// The name ImGui::ShowDemoWindow gives its window.
		constexpr const char* imgui_demo = "Dear ImGui Demo";

		// The ImGui settings entry that holds which panels are open.
		constexpr const char* panel_settings = "LoomEditor";

		constexpr ImGuiWindowFlags view_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;

		constexpr const char* camera_controls =
			"Right drag: look\n"
			"Right drag + WASD, QE: fly\n"
			"Shift: fly faster\n"
			"Middle drag: pan\n"
			"Wheel: zoom";

		#pragma endregion

		#pragma region Labels

		// Inspector field names longer than this are cut, however wide the panel.
		constexpr size_t max_label_chars = 20;

		// The most of the Inspector's width the label column takes before labels shrink.
		constexpr float max_label_share = 0.4f;

		// Significant digits with no trailing zeros, so 1 draws as 1 rather than 1.000.
		constexpr const char* float_format = "%.7g";
		constexpr const char* double_format = "%.15g";

		constexpr float drag_speed = 0.01f;

		// One a pixel, as ImGui drags a whole number by default.
		constexpr float integer_drag_speed = 1.0f;

		// A MultilineText field's box shows its lines and one more to type on, within these.
		constexpr int min_text_lines = 3;
		constexpr int max_text_lines = 12;

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

		// Labels sit left in a column as wide as the longest; longest seeds it.
		float LabelColumn(const std::vector<std::string>& labels, float longest)
		{
			for (const std::string& label : labels)
				longest = (std::max)(longest, ImGui::CalcTextSize(Shortened(label, FLT_MAX).c_str()).x);

			return (std::min)(longest, ImGui::GetContentRegionAvail().x * max_label_share);
		};

		// Wide enough for each label as deep as its categories nest it.
		float FieldLabelColumn(const LoomObject& object, const std::vector<std::string>& labels)
		{
			const std::vector<SerializedField>& fields = object.GetFields();
			float longest = 0.0f;

			for (size_t i = 0; i < labels.size(); i++)
			{
				if (!object.IsShown(fields[i]))
					continue;

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

		// A component whose type is not loaded, with its fields as the file has them.
		void DrawKept(const MissingComponent& missing)
		{
			ImGui::TextDisabled("Not loaded");

			std::vector<std::string> labels{ };

			for (const MissingComponent::Line& line : missing.lines)
				labels.push_back("Field " + line.key);

			const float column = LabelColumn(labels, 0.0f);

			for (size_t i = 0; i < labels.size(); i++)
			{
				Label(labels[i], column);
				ImGui::TextUnformatted(missing.lines[i].value.c_str());
			};
		};

		#pragma endregion

		#pragma region Sprites

		bool IsSpriteSource(const std::string& path)
		{
			return std::filesystem::path(path).extension() == SpriteAsset::extension || ProjectAssets::IsTextureFile(path);
		};

		// The texture, or the sprite flattened to an image, with forward slashes.
		std::string SpriteImage(const std::string& path)
		{
			if (std::filesystem::path(path).extension() == SpriteAsset::extension)
				return std::filesystem::path(SpriteAsset::ImageOf(path)).generic_string();

			return path;
		};

		#pragma endregion

		#pragma region Dragging

		// Components carry no name, so one shows as its type; a scene root as the scene.
		std::string Describe(const LoomObject& object)
		{
			if (const ComponentBase* component = dynamic_cast<const ComponentBase*>(&object))
				return ComponentRegistry::NameOf(*component) + " on " + Describe(*component->GetGameObject());

			if (const GameObject* gameObject = dynamic_cast<const GameObject*>(&object); gameObject && gameObject->GetParent() == nullptr)
				if (const Scene* scene = EditCommands::SceneOf(*gameObject))
					return scene->NameAndID();

			return object.NameAndID();
		};

		void DragSource(const LoomObject& object, ImGuiDragDropFlags flags = 0)
		{
			if (!ImGui::BeginDragDropSource(flags))
				return;

			ImGui::SetDragDropPayload(Editor::object_payload, &object.GetGuid(), sizeof(Guid));
			ImGui::TextUnformatted(Describe(object).c_str());

			ImGui::EndDragDropSource();
		};

		// The component of gameObject being dragged, or null when none is.
		ComponentBase* DraggedComponent(const GameObject& gameObject)
		{
			const ImGuiPayload* payload = ImGui::GetDragDropPayload();

			if (payload == nullptr || !payload->IsDataType(Editor::object_payload))
				return nullptr;

			return ComponentDrag::Dragged(gameObject, *(const Guid*)payload->Data);
		};

		// Folded while one of its components is dragged, then restored.
		bool ComponentHeader(const char* label, const GameObject& gameObject)
		{
			return ComponentDrag::Header(label, header_flags, DraggedComponent(gameObject) != nullptr);
		};

		// Null when nothing fits, which also keeps the field from lighting up under a refused drag.
		LoomObject* DraggedInto(const SerializedField& field)
		{
			const ImGuiPayload* payload = ImGui::GetDragDropPayload();

			if (payload == nullptr || !payload->IsDataType(Editor::object_payload))
				return nullptr;

			return field.ReferenceFor(LoomObject::GetByGuid(*(const Guid*)payload->Data));
		};

		#pragma endregion

		#pragma region Fields

		// The GameObject with this id, or null when it is gone.
		GameObject* LiveObject(uint64_t id)
		{
			return const_cast<GameObject*>(dynamic_cast<const GameObject*>(LoomObject::GetByID(id)));
		};

		// The index among object's fields of the one whose value is at data.
		size_t FieldAt(const LoomObject& object, const void* data)
		{
			const std::vector<SerializedField>& fields = object.GetFields();

			return std::find_if(
				fields.begin(),
				fields.end(),
				[data](const SerializedField& field) { return field.data == data; }) - fields.begin();
		};

		// Each object's field at index as text, for a play-mode undo step.
		std::vector<std::string> Written(const std::vector<LoomObject*>& objects, size_t index)
		{
			std::vector<std::string> written{ };

			for (const LoomObject* object : objects)
				written.push_back(object->GetFields()[index].Write());

			return written;
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

		// A value that is none of the choices shows as its number.
		bool DrawChoiceField(const char* label, const SerializedField& field, const std::vector<std::string>& choices, bool mixed)
		{
			const long long value = IntegerOf(field);
			const bool known = value >= 0 && value < (long long)choices.size();
			const std::string shown = mixed ? MIXED_VALUE : known ? choices[(size_t)value] : std::to_string(value);

			bool changed = false;

			if (ImGui::BeginCombo(label, shown.c_str()))
			{
				for (size_t i = 0; i < choices.size(); i++)
					if (ImGui::Selectable(choices[i].c_str(), (long long)i == value) && (mixed || (long long)i != value))
					{
						SetInteger(field, (long long)i);
						changed = true;
					};

				ImGui::EndCombo();
			};

			return changed;
		};

		// A value that is none of the enumerators shows as its number.
		bool DrawEnumField(const char* label, const SerializedField& field, const std::vector<FieldNames::Enumerator>& enumerators, bool mixed)
		{
			const long long value = IntegerOf(field);

			std::string shown = std::to_string(value);

			for (const FieldNames::Enumerator& enumerator : enumerators)
				if (enumerator.value == value)
					shown = enumerator.label;

			if (mixed)
				shown = MIXED_VALUE;

			bool changed = false;

			if (ImGui::BeginCombo(label, shown.c_str()))
			{
				for (const FieldNames::Enumerator& enumerator : enumerators)
					if (ImGui::Selectable(enumerator.label.c_str(), enumerator.value == value) &&
						(mixed || enumerator.value != value))
					{
						SetInteger(field, enumerator.value);
						changed = true;
					};

				ImGui::EndCombo();
			};

			return changed;
		};

		#pragma endregion
	};

	#pragma region Construction

	Editor::Editor() :
		m_dialogs(std::make_unique<EditorFileDialogs>())
	{
		EditorLog::Get().Install();


		// The editor draws into its own viewport, and the play controls own updating.
		Engine::renderScenes = false;
		Engine::updateScenes = false;

		Engine::SetGuiFunction([this]() { DrawGui(); });

		DialogueManagerBase::onNewBinding = [this](const std::string& type) { AddDialogueBinding(type); };

		// Read now, so the saved layout does not close panels a file opened from the command line shows.
		static const std::string layout = BesideExecutable("LoomEditor.layout.ini").string();
		ImGui::GetIO().IniFilename = layout.c_str();
		m_extensions.Load(extensionsFolder.empty() ? ExtensionLibrary::UserFolder() : extensionsFolder);
		AddPanelSettings();
		m_tornWindows.AddSettings();
		ImGui::LoadIniSettingsFromDisk(layout.c_str());

		// Floating windows stay inside the editor, or dialogs fall behind it.
		ImGui::GetIO().ConfigViewportsNoAutoMerge = false;

		// Clicking a number field types into it, Unity-style; dragging still scrubs.
		ImGui::GetIO().ConfigDragClickToInputText = true;

		ImGui::GetPlatformIO().Platform_OpenInShellFn = OpenInShell;
		ImGui::GetPlatformIO().Platform_OpenInShellUserData = this;

		m_windowFrame.Install(Engine::window, RedrawWhileHeld);

		window_editor = this;

		if (Engine::window)
			glfwSetWindowCloseCallback(Engine::window, OnClose);
		torn_frames = &m_tornFrames;
		destroy_platform_window = ImGui::GetPlatformIO().Platform_DestroyWindow;

		if (destroy_platform_window != nullptr)
			ImGui::GetPlatformIO().Platform_DestroyWindow = DestroyPlatformWindow;

		LoadSettings();
		m_settings.Apply();
	};

	Editor::~Editor()
	{
		// ImGui would call the handler after this is gone, and saving before reading would lose the rest.
		ImGuiIO& io = ImGui::GetIO();

		if (ImGui::GetCurrentContext()->SettingsLoaded)
			ImGui::SaveIniSettingsToDisk(io.IniFilename);

		io.IniFilename = nullptr;
		ImGui::RemoveSettingsHandler(panel_settings);
		m_tornWindows.RemoveSettings();
		m_extensions.Unload();

		SetDropCallback(nullptr);

		if (Engine::window)
			glfwSetWindowCloseCallback(Engine::window, nullptr);

		window_editor = nullptr;

		if (destroy_platform_window != nullptr)
			ImGui::GetPlatformIO().Platform_DestroyWindow = destroy_platform_window;

		m_tornFrames.clear();
		torn_frames = nullptr;

		Engine::SetGuiFunction(Task{ });

		DialogueManagerBase::onNewBinding = nullptr;

		Engine::renderScenes = true;
		Engine::updateScenes = true;

		// Queued work lands before its objects go, and while their script library is still loaded.
		ClosePrefab();
		m_opening.reset();
		Engine::DoTasks();

		for (Scene* scene : m_ownedScenes)
			delete scene;

		m_ownedScenes.clear();

		Engine::DoTasks();

		EditorLog::Get().Uninstall();
	};

	#pragma endregion

	#pragma region Settings

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

			// A file saved by hand can start with a byte order mark.
			if (first && line.rfind("\xEF\xBB\xBF", 0) == 0)
				line.erase(0, 3);

			first = false;

			if (line.rfind("project=", 0) == 0)
				project = line.substr(strlen("project="));

			// Read before the project opens, since opening it picks the starting scene.
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

		// From what the session last made active, since a rebuild saves with every scene closed.
		if (!m_startupScene.empty())
			out << "scene=" << m_startupScene << '\n';

		if (!m_currentLayout.empty())
			out << "layout=" << m_currentLayout << '\n';
	};

	std::string Editor::StartupScene() const
	{
		std::error_code code;

		// The last session's scene if it still belongs here; else the only one, or none.
		if (!m_startupScene.empty() &&
			m_startupScene.rfind(m_projectPath, 0) == 0 &&
			std::filesystem::exists(m_startupScene, code))
			return m_startupScene;

		return m_projectScenes.size() == 1
			? m_projectScenes.front()
			: "";
	};

	#pragma endregion

	#pragma region Project

	bool Editor::OpenProject(const std::string& path, bool open_scene)
	{
		std::error_code code;

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

		// So does the Scripts tab's file, which brings the tab forward.
		if (std::filesystem::path(path).extension() == ScriptFolders::extension &&
			std::filesystem::is_regular_file(path, code))
		{
			ShowProjectTab(ProjectTabs::Scripts);

			return OpenProject(std::filesystem::absolute(path, code).parent_path().string(), open_scene);
		};

		// And each FileTab's.
		for (FileTab& tab : m_fileTabs)
			if (std::filesystem::path(path).extension() == tab.kind.extension &&
				std::filesystem::is_regular_file(path, code))
			{
				ShowProjectTab(tab.kind.title);

				return OpenProject(std::filesystem::absolute(path, code).parent_path().string(), open_scene);
			};

		if (const std::filesystem::path file = path;
			(file.extension() == Prefab::extension ||
			file.extension() == SpriteAsset::extension ||
			file.extension() == Palette::extension ||
			file.extension() == BuildSettings::extension ||
			file.extension() == Shader::pixel_extension ||
			file.extension() == Shader::compute_extension ||
			file.extension() == PreVertex::extension) &&
			std::filesystem::is_regular_file(file, code))
		{
			const std::filesystem::path absolute = std::filesystem::absolute(file, code).lexically_normal();

			if (!OpenProject(ProjectFolderOf(absolute).string(), open_scene))
				return false;

			// A prefab's components need the scripts, whose build reopens it.
			if (file.extension() == BuildSettings::extension)
				m_showBuildSettings = true;
			else if (file.extension() == Prefab::extension && m_scripts.IsBuilding())
				m_prefabBeforeBuild = ProjectAssets::RelativeTo(absolute.string(), m_projectPath);
			else OpenAsset(absolute.string());

			return true;
		};

		if (std::filesystem::path(path).extension() == SceneSerializer::extension &&
			std::filesystem::is_regular_file(path, code))
		{
			const std::filesystem::path scene = std::filesystem::absolute(path, code).lexically_normal();

			m_startupScene = scene.string();

			if (!OpenProject(ProjectFolderOf(scene).string(), open_scene))
				return false;

			if (open_scene && !m_scripts.IsBuilding())
			{
				const auto open = std::find_if(m_scenePaths.begin(), m_scenePaths.end(),
					[&](const auto& entry) { return entry.second == m_startupScene; });

				if (open != m_scenePaths.end())
					SetActiveScene(open->first);
				else if (m_openingPath != m_startupScene)
					BeginOpeningScene(m_startupScene);
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

		if (resolved == m_projectPath)
		{
			RefreshProjectAssets();

			if (!m_scripts.HasProject())
				LoadProjectFile();

			if (open_scene && !m_scripts.IsBuilding() && m_ownedScenes.empty() && !m_opening)
				if (const std::string scene = StartupScene(); !scene.empty())
					BeginOpeningScene(scene);

			return true;
		};

		Engine::updateScenes = false;
		m_playing = false;
		m_playAfterCompile = false;
		m_playSnapshots.clear();
		m_snapshots.clear();

		Audio::Unload();

		m_opening.reset();
		m_openingPath.clear();
		m_projectLoad.reset();

		// Ahead of the scenes, whose closing deletes it while its scripts are still loaded.
		ClosePrefab();
		CloseAllScenes();
		m_prefabs.clear();
		m_readInstances.clear();
		m_instances.clear();
		m_linkedInstances.clear();

		m_projectPath = resolved;
		EditorLog::Get().SetProject(m_projectPath);

		// Asset paths are relative to the project.
		std::filesystem::current_path(m_projectPath, code);

		LoadProjectFile();

		RefreshProjectAssets();
		SaveSettings();

		// Not while building: PollScripts opens it once its component types exist.
		const std::string scene = open_scene && m_ownedScenes.empty()
			? StartupScene()
			: "";

		if (!scene.empty() && !m_scripts.IsBuilding())
			BeginOpeningScene(scene);

		if (!m_scripts.IsIdle())
			m_projectLoad = ProjectLoad{ true, m_ownedScenes.empty() && !StartupScene().empty() };
		else if (m_opening)
			m_projectLoad = ProjectLoad{ false, true };

		return true;
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
		// AddChild defers, so the scenes are only whole once the queue has run.
		Engine::DoTasks();

		// Taken before the prefabs sync, which marks a scene their change reached.
		m_history.Collect(m_ownedScenes);
		const std::vector<Scene*> unsaved = UnsavedScenes();
		RecordHistory();

		bool saved = true;

		for (Scene* scene : unsaved)
			saved = SaveToProject(scene) && saved;

		saved = m_shaders.SaveAll() && saved;
		saved = m_dialogues.SaveAll() && saved;
		saved = m_sprites.SaveAll() && saved;
		saved = m_models.SaveAll() && saved;

		if (m_keyBindings.Unsaved())
			saved = SaveInputActions() && saved;

		if (m_buildSettings.Unsaved())
			saved = SaveBuildSettings() && saved;

		if (m_prefabUnsaved)
			saved = SavePrefab() && saved;

		SetActiveScene(m_activeScene);

		return saved;
	};

	void Editor::PollRefresh()
	{
		// Mid-build the scenes are down, held as text until the scripts are back.
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

	void Editor::ApplyUpdate()
	{
		if (m_update.Waiting() == EditorUpdate::Kind::None)
			return;

		const bool busy =
			!m_scripts.IsIdle() || IsRefreshing() || m_opening || m_projectLoad ||
			m_gameBuilder.GetStatus() == GameBuilder::Status::Building || m_webPlayer.IsBuilding() || m_tests.HasQueued() ||
			(m_updateSaveFailed && !UnsavedWork().empty()) ||
			ImGui::IsAnyItemActive() || ImGui::IsAnyMouseDown();

		if (!busy)
			DropSaveQuestions();

		const EditorUpdate::Kind due = m_update.Due(busy || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));

		if (due == EditorUpdate::Kind::None)
			return;

		Stop();

		if (due == EditorUpdate::Kind::Scripts)
		{
			CompileScripts();
			return;
		};

		m_updateSaveFailed = !SaveProject();

		if (m_updateSaveFailed)
		{
			std::cerr << "Update: not everything saved, so this editor stays up until it is." << std::endl;
			m_update.Request(due);
			return;
		};

		m_refresh.NoteExecutable();
		m_relaunch = true;
		Engine::Stop();
	};

	void Editor::DropSaveQuestions()
	{
		m_saveQuestion.reset();
		m_prefabNext.reset();
		m_shaders.DropCloseQuestion();
		m_dialogues.DropCloseQuestion();
		m_sprites.DropCloseQuestion();
		m_models.DropCloseQuestion();
	};

	#pragma endregion

	#pragma region Scenes

	Scene* Editor::CreateScene(const std::string& name)
	{
		Scene* scene = new Scene(name);
		RenderingObjects::AddStarter(scene->GetRoot(), ShapeShader());

		// Children join their parents through the queue, and a new scene asset is saved straight away.
		Engine::DoTasks();

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

		Scene* scene = SceneSerializer::LoadFromFile(path, &error);

		if (scene == nullptr)
		{
			std::cerr << "Could not open scene: " << error << std::endl;
			return nullptr;
		};

		AdoptScene(scene, path);

		return scene;
	};

	void Editor::AdoptScene(Scene* scene, const std::string& path)
	{
		m_ownedScenes.push_back(scene);
		m_historyDirty = true;
		m_scenePaths[scene] = std::filesystem::path(path).lexically_normal().string();
		m_history.Saved(*scene);

		SetActiveScene(scene);

		// A model being previewed stays in the Inspector.
		if (m_selectedModel.empty())
			Select(&scene->GetRoot());
	};

	bool Editor::OpenSceneAlone(const std::string& path)
	{
		if (!m_scripts.IsIdle())
		{
			std::cerr << "The scenes are down while the scripts build; open it once they are back." << std::endl;
			return false;
		};

		Stop();

		const std::string normal = std::filesystem::path(path).lexically_normal().string();

		if (m_opening && m_openingPath == normal)
			return true;

		if (m_ownedScenes.size() == 1 && ScenePathOf(m_ownedScenes.front()) == normal)
		{
			SetActiveScene(m_ownedScenes.front());
			return true;
		};

		std::vector<std::string> unsaved{ };

		for (Scene* scene : UnsavedScenes())
			unsaved.push_back(scene->GetName());

		AskToSave(
			unsaved,
			[this]()
			{
				for (Scene* scene : UnsavedScenes())
					if (!SaveToProject(scene))
						return false;

				return true;
			},
			[this, normal]()
			{
				const std::vector<Scene*> closing = m_ownedScenes;

				for (Scene* scene : closing)
					CloseScene(scene);

				RefreshProjectAssets();
				BeginOpeningScene(normal);
			});

		return true;
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

	bool Editor::SceneUnsaved(Scene* scene) const
	{
		return ScenePathOf(scene).empty() || m_history.Unsaved(*scene);
	};

	std::vector<Scene*> Editor::UnsavedScenes() const
	{
		std::vector<Scene*> unsaved{ };

		for (Scene* scene : m_ownedScenes)
			if (SceneUnsaved(scene))
				unsaved.push_back(scene);

		return unsaved;
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

		if (IsSelectedIn(scene->GetRoot()))
			Select(nullptr);

		m_scenePaths.erase(scene);

		// Deleting mid-GUI would leave the rest of the frame on a dangling pointer.
		Engine::QueueTask([scene]() { delete scene; });
	};

	void Editor::SetActiveScene(Scene* scene)
	{
		m_activeScene = scene;

		// Closing scenes for a rebuild is not the user leaving one.
		if (scene == nullptr || m_projectPath.empty())
			return;

		const auto path = m_scenePaths.find(scene);

		if (path != m_scenePaths.end() && !path->second.empty())
			m_startupScene = path->second;

		SaveSettings();
	};

	#pragma endregion

	#pragma region Saving

	bool Editor::SaveScene(Scene* scene, const std::string& path)
	{
		// A run's state is not the scene's.
		if (scene == nullptr || m_playing)
			return false;

		// What is saved is what the history marks, so it has to have seen it.
		RecordHistory();

		std::string error;

		if (!SceneSerializer::SaveToFile(*scene, path, &error))
		{
			std::cerr << "Could not save scene: " << error << std::endl;
			return false;
		};

		m_scenePaths[scene] = std::filesystem::path(path).lexically_normal().string();
		m_history.Saved(*scene);

		return true;
	};

	void Editor::SaveActiveScene()
	{
		if (m_activeScene == nullptr || m_playing)
			return;

		if (SaveToProject(m_activeScene))
			RefreshProjectAssets();
	};

	void Editor::Quit()
	{
		Stop();

		AskToSave(UnsavedWork(), [this]() { return SaveProject(); }, []() { Engine::Stop(); });
	};

	std::vector<std::string> Editor::UnsavedWork() const
	{
		std::vector<std::string> unsaved{ };

		for (Scene* scene : UnsavedScenes())
			unsaved.push_back(scene->GetName());

		if (m_prefabUnsaved)
			unsaved.push_back(std::filesystem::path(m_prefabPath).filename().string());

		const std::vector<std::string> panels[] =
		{
			m_shaders.UnsavedFiles(),
			m_dialogues.UnsavedFiles(),
			m_sprites.UnsavedFiles(),
			m_models.UnsavedFiles(),
		};

		for (const std::vector<std::string>& files : panels)
			unsaved.insert(unsaved.end(), files.begin(), files.end());

		if (m_keyBindings.Unsaved())
			unsaved.push_back("Key Bindings");

		if (m_buildSettings.Unsaved())
			unsaved.push_back("Build Settings");

		return unsaved;
	};

	#pragma endregion

	#pragma region Refresh

	void Editor::FullRefresh()
	{
		if (!IsRefreshing())
			m_refresh.Start();
	};

	std::string Editor::RelaunchTarget() const
	{
		const auto scene = m_activeScene ? m_scenePaths.find(m_activeScene) : m_scenePaths.end();

		return scene != m_scenePaths.end() ? scene->second : m_projectPath;
	};

	#pragma endregion

	#pragma region Prefabs

	void Editor::OpenPrefab(const std::string& path)
	{
		// Closing the one open clears the path this may have been handed.
		const std::string opening = path;

		if (m_prefabUnsaved)
		{
			if (opening == m_prefabPath)
			{
				m_showPrefab = true;
				m_prefabFocusFrames = prefab_focus_frames;
			}
			else m_prefabNext = opening;

			return;
		};

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

		// Agreed with the file next pass, once the copy has its children.
		m_prefabShape.clear();
		m_prefabOpened = true;
		m_historyDirty = true;

		m_showPrefab = true;
		m_prefabFocusFrames = prefab_focus_frames;

		Select(root);
	};

	void Editor::ClosePrefab()
	{
		if (m_prefabScene == nullptr)
			return;

		if (IsSelectedIn(m_prefabScene->GetRoot()))
			Select(nullptr);

		// Deferred, as CloseScene's is, past whatever the frame still draws.
		Scene* scene = std::exchange(m_prefabScene, nullptr);
		Engine::QueueTask([scene]() { delete scene; });

		m_prefabRoot = Guid();
		m_prefabPath.clear();
		m_prefabShape.clear();
		m_reshowPrefab = false;
		m_prefabUnsaved = false;
		m_prefabFocused = false;
	};

	bool Editor::SavePrefab()
	{
		if (GetPrefabRoot() == nullptr || !CanStepHistory())
			return false;

		m_savePrefab = true;
		RecordHistory();
		m_savePrefab = false;

		return !m_prefabUnsaved;
	};

	GameObject* Editor::GetPrefabRoot() const
	{
		GameObject* root = m_prefabScene ? LoomObject::GetByGuid<GameObject>(m_prefabRoot) : nullptr;

		return root && Hierarchy::Contains(m_prefabScene->GetRoot(), root) ? root : nullptr;
	};

	void Editor::DrawPrefabNode()
	{
		GameObject* root = GetPrefabRoot();

		if (root == nullptr)
			return;

		ImGui::Separator();

		const std::string lead = std::string(EditorIcons::Prefab) + " ";
		const std::string shown = FitPath(
			m_prefabPath,
			ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(lead.c_str()).x);

		ImGui::TextDisabled("%s%s", lead.c_str(), shown.c_str());

		if (shown != m_prefabPath)
			ImGui::SetItemTooltip("%s", m_prefabPath.c_str());

		DrawGameObjectNode(root, false, false);
	};

	#pragma endregion

	#pragma region Selection

	void Editor::Select(GameObject* gameObject)
	{
		m_selected = gameObject;
		m_selectedId = gameObject ? gameObject->m_ID : 0;
		m_selection.assign(gameObject ? 1 : 0, m_selectedId);
		m_nameBufferOwner = nullptr;
		m_selectedModel.clear();
		m_renaming = false;
		m_newAssetFor = nullptr;
		m_newAssetMade.clear();
	};

	void Editor::SetSelection(const std::vector<GameObject*>& objects)
	{
		Select(objects.empty() ? nullptr : objects.front());

		if (m_selected == nullptr || m_selected->GetParent() == nullptr)
			return;

		for (size_t i = 1; i < objects.size(); i++)
			if (objects[i]->GetParent() && !IsSelected(*objects[i]))
				m_selection.push_back(objects[i]->m_ID);
	};

	std::vector<GameObject*> Editor::GetSelection() const
	{
		std::vector<GameObject*> selection{ };

		if (m_selected == nullptr)
			return selection;

		selection.push_back(m_selected);

		for (const uint64_t id : m_selection)
			if (GameObject* gameObject = LiveObject(id); gameObject && id != m_selectedId)
				selection.push_back(gameObject);

		return selection;
	};

	bool Editor::IsSelected(const GameObject& gameObject) const
	{
		return std::find(m_selection.begin(), m_selection.end(), gameObject.m_ID) != m_selection.end();
	};

	std::vector<Guid> Editor::GetSelectedGuids() const
	{
		std::vector<Guid> guids{ };

		for (const GameObject* gameObject : GetSelection())
			guids.push_back(gameObject->GetGuid());

		return guids;
	};

	void Editor::SelectAgain(const std::vector<Guid>& guids)
	{
		std::vector<GameObject*> found{ };

		for (const Guid& guid : guids)
			if (GameObject* gameObject = LoomObject::GetByGuid<GameObject>(guid))
				found.push_back(gameObject);

		if (!found.empty())
			SetSelection(found);
	};

	std::vector<GameObject*> Editor::HierarchyRows() const
	{
		const auto open = [this](const GameObject& node) { return ShownOpen(node); };

		std::vector<GameObject*> rows{ };

		const auto add = [&](GameObject& parent)
		{
			const std::vector<GameObject*> under = MultiEdit::Rows(parent, open);

			rows.insert(rows.end(), under.begin(), under.end());
		};

		for (Scene* scene : Scene::GetScenes())
			if (ShownOpen(scene->GetRoot()))
				add(scene->GetRoot());

		if (GameObject* prefab = GetPrefabRoot())
		{
			rows.push_back(prefab);

			if (ShownOpen(*prefab))
				add(*prefab);
		};

		return rows;
	};

	void Editor::ApplySelection(const ImGuiMultiSelectIO& io)
	{
		if (io.Requests.empty())
			return;

		const bool root = m_selected && m_selected->GetParent() == nullptr;

		std::vector<GameObject*> selection = root ? std::vector<GameObject*>{ } : GetSelection();
		std::vector<GameObject*> rows{ };
		GameObject* clicked = nullptr;

		for (const ImGuiSelectionRequest& request : io.Requests)
		{
			if (request.Type == ImGuiSelectionRequestType_SetAll)
			{
				selection = request.Selected ? HierarchyRows() : std::vector<GameObject*>{ };
				continue;
			};

			GameObject* first = LiveObject((uint64_t)request.RangeFirstItem);
			GameObject* last = LiveObject((uint64_t)request.RangeLastItem);

			if (first == nullptr || last == nullptr)
				continue;

			if (first != last && rows.empty())
				rows = HierarchyRows();

			const std::vector<GameObject*> range = first == last
				? std::vector<GameObject*>{ first }
				: MultiEdit::Range(rows, first, last);

			for (GameObject* row : range)
			{
				const auto found = std::find(selection.begin(), selection.end(), row);

				if (request.Selected && found == selection.end())
					selection.push_back(row);
				else if (!request.Selected && found != selection.end())
					selection.erase(found);
			};

			if (request.Selected)
				clicked = last;
		};

		const auto has = [&selection](const GameObject* gameObject)
		{
			return std::find(selection.begin(), selection.end(), gameObject) != selection.end();
		};

		// The row clicked last is the one the Inspector is drawn from, as in Unity.
		GameObject* primary =
			clicked && has(clicked) ? clicked :
			has(m_selected) ? m_selected :
			selection.empty() ? nullptr : selection.back();

		if (primary)
		{
			selection.erase(std::find(selection.begin(), selection.end(), primary));
			selection.insert(selection.begin(), primary);
		};

		const std::vector<GameObject*> now = GetSelection();

		// A click on a row selected alone changes nothing, so its double-click can rename it.
		if (!root &&
			primary == m_selected &&
			selection.size() == now.size() &&
			std::is_permutation(selection.begin(), selection.end(), now.begin()))
			return;

		SetSelection(selection);
	};

	void Editor::SelectModel(const std::string& path)
	{
		Select(nullptr);
		m_selectedModel = path;
		m_selectedAsset = std::filesystem::path(path).lexically_normal().string();
	};

	bool Editor::IsSelectedIn(const GameObject& root) const
	{
		return
			m_selected &&
			LoomObject::GetByID(m_selectedId) == m_selected &&
			Hierarchy::Contains(root, m_selected);
	};

	void Editor::RequestSelection(const std::string& name)
	{
		if (m_pendingSelection.empty())
			m_pendingSelectionAdds = false;

		m_pendingSelection.push_back(name);
		m_selectionFramesLeft = selection_frames;
	};

	void Editor::ValidateSelection()
	{
		const std::vector<Scene*>& scenes = Scene::GetScenes();

		// Scenes add their children through the queue, so the name is looked for over a few frames.
		if (!m_pendingSelection.empty() && !m_scripts.IsBuilding())
		{
			while (!m_pendingSelection.empty())
			{
				GameObject* named = nullptr;

				for (Scene* scene : scenes)
					if ((named = FindByName(scene->GetRoot(), m_pendingSelection.front())))
						break;

				if (named == nullptr)
					break;

				std::vector<GameObject*> chosen = m_pendingSelectionAdds ? GetSelection() : std::vector<GameObject*>{ };

				chosen.push_back(named);
				SetSelection(chosen);

				m_pendingSelectionAdds = true;
				m_pendingSelection.erase(m_pendingSelection.begin());
			};

			if (!m_pendingSelection.empty() && --m_selectionFramesLeft <= 0)
			{
				for (const std::string& name : m_pendingSelection)
					std::cerr << "No GameObject named '" << name << "'" << std::endl;

				m_pendingSelection.clear();
			};
		};

		if (m_activeScene &&
			std::find(scenes.begin(), scenes.end(), m_activeScene) == scenes.end())
			m_activeScene = nullptr;

		if (m_activeScene == nullptr && !scenes.empty())
			m_activeScene = scenes.front();

		const auto gone = [&](uint64_t id)
		{
			const GameObject* gameObject = dynamic_cast<const GameObject*>(LoomObject::GetByID(id));

			if (gameObject == nullptr)
				return true;

			for (Scene* scene : scenes)
				if (Hierarchy::Contains(scene->GetRoot(), gameObject))
					return false;

			return !(m_prefabScene && Hierarchy::Contains(m_prefabScene->GetRoot(), gameObject));
		};

		if (std::erase_if(m_selection, gone) == 0)
			return;

		if (m_selection.empty())
			Select(nullptr);
		else if (std::find(m_selection.begin(), m_selection.end(), m_selectedId) == m_selection.end())
		{
			m_selected = LiveObject(m_selection.back());
			m_selectedId = m_selection.back();
		};

		// The name box shows what the ones left have in common.
		m_nameBufferOwner = nullptr;
	};

	#pragma endregion

	#pragma region Editing

	void Editor::DeleteSelected()
	{
		if (!EditCommands::CanTake(m_selected))
			return;

		for (GameObject* gameObject : MultiEdit::Tops(GetSelection()))
			if (EditCommands::CanTake(gameObject))
				gameObject->Destroy();

		Select(nullptr);

		m_historyDirty = true;
	};

	void Editor::Copy()
	{
		if (!EditCommands::CanTake(m_selected))
			return;

		std::string text{ };

		for (GameObject* gameObject : MultiEdit::Tops(GetSelection()))
			if (EditCommands::CanTake(gameObject))
				text += SceneSerializer::Serialize(*gameObject);

		ImGui::SetClipboardText(text.c_str());
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

		const std::vector<GameObject*> pasted = SceneSerializer::DeserializeAll(text, *parent, &error);

		if (pasted.empty())
		{
			std::cerr << "Nothing to paste: " << error << std::endl;
			return;
		};

		SetSelection(pasted);

		m_historyDirty = true;
	};

	#pragma endregion

	#pragma region History

	void Editor::Undo()
	{
		if (m_playing)
		{
			RecordPlayEdit();

			if (const std::vector<GameObject*> stepped = m_playHistory.Undo(); !stepped.empty())
				SetSelection(stepped);

			return;
		};

		if (PrefabHasKeyboard())
			StepPrefab(true);
		else StepHistory(&SceneHistory::Undo);
	};

	void Editor::Redo()
	{
		if (m_playing)
		{
			RecordPlayEdit();

			if (const std::vector<GameObject*> stepped = m_playHistory.Redo(); !stepped.empty())
				SetSelection(stepped);

			return;
		};

		if (PrefabHasKeyboard())
			StepPrefab(false);
		else StepHistory(&SceneHistory::Redo);
	};

	void Editor::StepHistory(bool (SceneHistory::* step)(const std::vector<Scene*>&))
	{
		if (!CanStepHistory())
			return;

		std::vector<std::pair<Scene*, Guid>> selected{ };

		for (GameObject* gameObject : GetSelection())
			if (Scene* scene = m_history.SceneOf(*gameObject))
				selected.emplace_back(scene, gameObject->GetGuid());

		if (!(m_history.*step)(m_ownedScenes))
			return;

		// The prefabs follow a step as they do an edit, and the name box reads the stepped name.
		m_historyDirty = true;
		m_nameBufferOwner = nullptr;

		if (selected.empty())
			return;

		const std::vector<GameObject*> before = GetSelection();
		std::vector<GameObject*> again{ };

		for (const auto& [scene, guid] : selected)
			if (GameObject* found = m_history.Find(*scene, guid))
				again.push_back(found);

		if (again == before)
			return;

		SetSelection(again);
	};

	bool Editor::CanStepHistory() const
	{
		// Mid-build every step would belong to a closed scene.
		return !m_playing && m_scripts.IsIdle();
	};

	bool Editor::CanUndo() const
	{
		return m_playing
			? m_playHistory.CanUndo()
			: CanStepHistory() && (PrefabHasKeyboard() ? m_prefabHistory.CanUndo() : m_history.CanUndo());
	};

	bool Editor::CanRedo() const
	{
		return m_playing
			? m_playHistory.CanRedo()
			: CanStepHistory() && (PrefabHasKeyboard() ? m_prefabHistory.CanRedo() : m_history.CanRedo());
	};

	void Editor::TrackPlayEdit(const std::vector<LoomObject*>& objects, size_t index, const std::vector<std::string>& before)
	{
		if (!m_playEdits.empty() && m_playEdits.front().object == objects.front() && m_playEdits.front().index == index)
			return;

		RecordPlayEdit();

		for (size_t i = 0; i < objects.size(); i++)
			m_playEdits.push_back(PlayEdit{ objects[i], objects[i]->m_ID, index, before[i] });
	};

	void Editor::RecordPlayEdit()
	{
		std::vector<PlayHistory::Field> edits{ };

		for (const PlayEdit& edit : m_playEdits)
			if (m_playing && LoomObject::GetByID(edit.id) == edit.object)
			{
				PlayHistory::Field& field = edits.emplace_back();

				field.before = edit.before;
				field.object = edit.object->GetGuid();
				field.index = edit.index;
			};

		m_playHistory.RecordFields(std::move(edits));
		m_playEdits.clear();
	};

	void Editor::ChangeComponents(GameObject& gameObject, const std::function<void()>& change)
	{
		ChangeComponents({ &gameObject }, [&change](size_t) { change(); });
	};

	void Editor::ChangeComponents(const std::vector<GameObject*>& objects, const std::function<void(size_t)>& change)
	{
		if (!m_playing)
		{
			for (size_t i = 0; i < objects.size(); i++)
				change(i);

			return;
		};

		// The held edit goes first, since the change may take its object off.
		RecordPlayEdit();

		std::vector<PlayHistory::Components> before{ };

		for (GameObject* gameObject : objects)
			before.push_back(PlayHistory::ReadComponents(*gameObject));

		for (size_t i = 0; i < objects.size(); i++)
			change(i);

		// Removing a component waits for the queue, so the change is read once it gets here.
		Engine::QueueTask(
			[this, before = std::move(before)]()
			{
				if (m_playing)
					m_playHistory.RecordComponents(before);
			});
	};

	void Editor::RecordHistory()
	{
		m_historyDirty = false;

		if (m_playing)
			return;

		m_history.Collect(m_ownedScenes);

		const std::unordered_set<GameObject*> changed = m_history.TakeChanged();

		if (!changed.empty() || PrefabsNeedSync())
			SyncPrefabs(changed);

		m_history.Record(m_ownedScenes);

		// What the sync changed is its own doing, not an edit for it to carry.
		m_history.TakeChanged();
	};

	bool Editor::CanEditScenes(const ImGuiWindow* window) const
	{
		if (window == nullptr)
			return true;

		const ImGuiWindow* root = window->RootWindow;

		for (const char* files_only : { EditorPanels::Shaders, EditorPanels::Dialogues, EditorPanels::Sprites })
			if (root == ImGui::FindWindowByName(files_only))
				return false;

		return root != ImGui::FindWindowByName(EditorPanels::Project) || m_projectTab == ProjectTabs::Scenes;
	};

	#pragma endregion

	#pragma region Workspace

	void Editor::HandleShortcuts()
	{
		// Wherever the keyboard is: it is about the whole editor.
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_R))
			FullRefresh();

		if ((m_showSprites && m_sprites.IsFocused()) || (m_showPalette && m_sprites.IsPaletteFocused()))
			return;

		if (m_showModels && m_models.IsFocused())
			return;

		if ((m_showKeyBindings && m_keyBindings.IsFocused()) || (m_showBuildSettings && m_buildSettings.IsFocused()))
			return;

		if ((m_showShaders && m_shaders.IsFocused()) || (m_showDialogues && m_dialogues.IsFocused()))
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
			if (PrefabHasKeyboard())
				SavePrefab();
			else SaveActiveScene();

			break;
		};
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

		// A run changes the scenes every frame, and Stop puts them back.
		LoomObject::Watch(!m_playing);

		if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyMouseDown())
		{
			RecordPlayEdit();

			if (m_historyDirty)
				RecordHistory();
		};

		if (!m_historySteps.empty() && m_importQueue.empty() && !m_historyDirty && CanStepHistory())
		{
			m_historySteps.front() ? Undo() : Redo();
			m_historySteps.erase(m_historySteps.begin());
		};

		HandleShortcuts();
		PollShaders();
		ReloadSavedSprites();
		TakeSavedModels();

		// Panels come and go as OS windows, so the callback goes on whichever exist.
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

		Input::SetView(0, 0, 0, 0);
		m_gamePanelFocused = false;
		m_gamePanelHovered = false;
		m_flyingView = false;
#ifndef LOOM_FINAL
		m_stagesShown = false;
#endif

		if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
			m_fullscreen ? LeaveFullscreen() : EnterFullscreen(m_lastView);

		for (const ExtensionRegistry::Panel& panel : ExtensionRegistry::Panels())
			if (panel.update)
				panel.update(m_projectPath);

		if (m_fullscreen)
			DrawFullscreen();
		else
			DrawWorkspace();

		DrawLoading();

#ifndef LOOM_FINAL
		Profiler::SetRecording(m_stagesShown || UiScript::RecordingStages());
#endif
		Input::SetFocused(m_gameFocus.Update(m_gamePanelFocused, m_gamePanelHovered, AnyMouseClicked(), ImGui::IsAnyMouseDown() || AnyMouseReleased()));

		m_tabHistory.Update(!(IsPlaying() && Input::IsFocused()));

		ImportQueued();
		ClickQueued();
		PollScripts();
		RunQueuedTests();
		StepOpening();
		PollRefresh();
		ApplyUpdate();

		// The renderer reads the window back after drawing, so the bitmap is the whole editor.
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
		DrawSaveQuestion();

		if (m_flyingView)
			return;

		const ImGuiContext& context = *ImGui::GetCurrentContext();

		if (ImGui::IsAnyItemActive() && CanEditScenes(context.ActiveIdWindow))
			m_historyDirty = true;

		for (int button = 0; button < ImGuiMouseButton_COUNT; button++)
			if (ImGui::IsMouseReleased(button) && CanEditScenes(context.HoveredWindow))
				m_historyDirty = true;
	};

	void Editor::DrawWorkspace()
	{
		DrawMainMenuBar();

		// Docked panels keep no size limits, so the Project panel's icons set one.
		const ImVec2 min_size = ImGui::GetStyle().WindowMinSize;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2((std::max)(min_size.x, m_showProject ? m_projectMinWidth : 0.0f), min_size.y));

		const ImGuiID dockspace_id =
			ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

		const ImVec2 padding = ImGui::GetStyle().FramePadding;
		m_tornWindows.Draw(ImVec2(padding.x, padding.y * menu_bar_padding), [this](const TornWindows::Torn& torn) { DrawTornBar(torn); });

		ImGui::PopStyleVar();

		if (m_firstFrame)
			m_checkLayout = true;

		// From the second run on, imgui.ini holds the user's layout.
		if (m_checkLayout)
		{
			const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace_id);

			if (node == nullptr || node->IsEmpty())
				m_rebuildLayout = true;

			// A layout saved before a window existed sends it to the middle or beside its panel.
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

				const auto dock_beside = [&placed](const char* window, const char* beside)
				{
					if (const ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(beside));
						!placed(window) && settings != nullptr && settings->DockId != 0)
						ImGui::DockBuilderDockWindow(window, settings->DockId);
				};

				for (const auto& [window, beside] : side_windows)
					dock_beside(window, beside);
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
		if (m_showHierarchy)	DrawHierarchy();
		if (m_showInspector)	DrawInspector();
		DrawPrefab();
		if (m_showGame)			DrawGame();
		if (m_showDebug)		DrawDebug();
		if (m_showSceneData)	DrawSceneData();
		if (m_showConsole)		m_console.Draw(&m_showConsole, [this](const EditorLog::Link& link) { OpenInVisualStudio(link.path, link.line, link.column); });
		if (m_showShaders)		m_shaders.Draw(&m_showShaders);
		if (m_showDialogues)	m_dialogues.Draw(&m_showDialogues);
		if (m_showModels)		m_models.Draw(&m_showModels);
		if (m_showSprites)		m_sprites.Draw(&m_showSprites, &m_showPalette);
		if (m_showPalette)		m_sprites.DrawPalette(&m_showPalette, m_projectPath);
		if (m_showStats)		DrawStats();
		// Not before the panel's first frame, whose dock would pick its saved tab over it.
		if (m_focusTests && ImGui::FindWindowByName(EditorPanels::Tests))
		{
			ImGui::SetNextWindowFocus();
			m_focusTests = false;
		};

		if (m_showTests)		m_tests.Draw(&m_showTests, { [this] { AskForAsset(AssetKind::Test, m_projectPath); }, [this](const std::string& file, int line) { OpenInVisualStudio(file, line); } });

		if (m_showKeyBindings && m_keyBindings.Draw(&m_showKeyBindings, !m_projectFile.empty()))
			SaveInputActions();

		if (m_showImGuiDemo)	ImGui::ShowDemoWindow(&m_showImGuiDemo);

		if (m_showSettings && m_settings.Draw(&m_showSettings))
			SaveSettings();

		if (m_showBuildSettings)
			DrawBuildSettings();

		for (ExtensionRegistry::Panel& panel : ExtensionRegistry::Panels())
		{
			if (!panel.shown)
				continue;

			if (const ImGuiWindow* inspector = ImGui::FindWindowByName(EditorPanels::Inspector); inspector != nullptr)
				ImGui::SetNextWindowDockID(inspector->DockId, ImGuiCond_FirstUseEver);

			if (ImGui::Begin(panel.title.c_str(), &panel.shown))
				panel.draw(m_projectPath);

			ImGui::End();
		};

		m_tornWindows.Prune();
	};

	void Editor::DrawFullscreen()
	{
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
		m_windowFrame.Fit();
	};

	void Editor::LeaveFullscreen()
	{
		m_fullscreen.reset();
		m_windowFullscreen.Leave(Engine::window);
		m_windowFrame.Fit();
	};

	void Editor::DrawMainMenuBar()
	{
		const ImVec2 padding = ImGui::GetStyle().FramePadding;

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding.x, padding.y * menu_bar_padding));

		if (!ImGui::BeginMainMenuBar())
		{
			ImGui::PopStyleVar();
			return;
		};

		DrawMenus();

		if (m_update.Waiting() != EditorUpdate::Kind::None)
		{
			ImGui::TextDisabled("Update waiting");
			ImGui::SetItemTooltip("Applies once idle");
		};

		if (DrawWindowButtons(Engine::window, m_windowFrame))
			Quit();

		ImGui::EndMainMenuBar();
		ImGui::PopStyleVar();
	};

	void Editor::DrawMenus()
	{
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

			if (ImGui::MenuItem("Build Settings...", nullptr, false, !m_projectFile.empty()))
			{
				m_showBuildSettings = true;
				m_focusBuildSettings = true;
			};

			ImGui::Separator();

			if (ImGui::BeginMenu("Open Demo"))
			{
				if (ImGui::IsWindowAppearing())
					m_demos = DemoProjects();

				if (m_demos.empty())
					ImGui::TextDisabled("No demos found.");

				for (const std::filesystem::path& demo : m_demos)
					if (ImGui::MenuItem(demo.filename().string().c_str()))
						LeaveFor(demo.string());

				ImGui::EndMenu();
			};

			ImGui::Separator();

			if (ImGui::MenuItem("Full Refresh", "Ctrl+Shift+R", false, !IsRefreshing()))
				FullRefresh();

			if (ImGui::MenuItem("Exit"))
				Quit();

			ImGui::EndMenu();
		};

		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, CanUndo()))
				Undo();

			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, CanRedo()))
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

			DrawShapeMenu(m_activeScene ? &m_activeScene->GetRoot() : nullptr);
			DrawCutsceneMenu(m_activeScene ? &m_activeScene->GetRoot() : nullptr);
			DrawRenderingMenu(m_activeScene ? &m_activeScene->GetRoot() : nullptr);

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

		if (ImGui::MenuItem("Play Web", nullptr, false, ScriptRun::Available && m_activeScene != nullptr && !m_projectPath.empty()))
			PlayWeb();

		if (!ScriptRun::Available)
			ImGui::SetItemTooltip("Windows only");
	};

	bool Editor::DrawWindowButtons(GLFWwindow* window, WindowFrame& frame)
	{
		if (!frame.IsInstalled())
			return false;

		const ImRect bar = ImGui::GetCurrentWindow()->MenuBarRect();
		const ImVec2 bar_min = bar.Min;
		const ImVec2 bar_max = bar.Max;
		const float height = bar_max.y - bar_min.y;
		const float width = height * window_button_aspect;
		const float buttons_left = bar_max.x - width * window_button_count;
		const float strip_left = ImGui::GetItemRectMax().x;
		const ImVec2 origin = ImGui::GetWindowViewport()->Pos;

		// A click on the bar has to reach ImGui to close an open menu.
		frame.SetDragStrip(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)
			? WindowFrame::Rect{ }
			: WindowFrame::Rect{ strip_left - origin.x, bar_min.y - origin.y, buttons_left - origin.x, bar_max.y - origin.y });

		if (const ImGuiMouseCursor cursor = ResizeCursor(frame.Hovered()); cursor != ImGuiMouseCursor_None)
			ImGui::SetMouseCursor(cursor);

		constexpr float centred = 0.5f;

		ImDrawList& draw = *ImGui::GetWindowDrawList();
		const float glyph = height * window_glyph_size;
		const float offset = glyph * window_glyph_offset;
		const bool maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
		ImVec2 center{ };
		ImU32 color = 0;

		const auto button = [&](int index, const char* id, ImU32 hovered, ImU32 pressed) -> bool
		{
			const ImVec2 corner(buttons_left + width * index, bar_min.y);

			ImGui::SetCursorScreenPos(corner);

			const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, height));

			center = ImVec2(corner.x + width * centred, corner.y + height * centred);
			color = ImGui::GetColorU32(ImGuiCol_Text);

			if (ImGui::IsItemHovered())
				draw.AddRectFilled(corner, ImVec2(corner.x + width, bar_max.y), ImGui::IsItemActive() ? pressed : hovered);

			return clicked;
		};

		draw.PushClipRect(bar_min, bar_max, false);

		if (button(0, "##Minimize", ImGui::GetColorU32(ImGuiCol_HeaderHovered), ImGui::GetColorU32(ImGuiCol_HeaderActive)))
			glfwIconifyWindow(window);

		draw.AddLine(ImVec2(center.x - glyph, center.y), ImVec2(center.x + glyph, center.y), color);

		if (button(1, "##Maximize", ImGui::GetColorU32(ImGuiCol_HeaderHovered), ImGui::GetColorU32(ImGuiCol_HeaderActive)))
		{
			if (maximized)
				glfwRestoreWindow(window);
			else glfwMaximizeWindow(window);
		};

		if (maximized)
		{
			draw.AddRect(ImVec2(center.x - glyph, center.y - glyph + offset), ImVec2(center.x + glyph - offset, center.y + glyph), color);
			draw.AddLine(ImVec2(center.x - glyph + offset, center.y - glyph + offset), ImVec2(center.x - glyph + offset, center.y - glyph), color);
			draw.AddLine(ImVec2(center.x - glyph + offset, center.y - glyph), ImVec2(center.x + glyph, center.y - glyph), color);
			draw.AddLine(ImVec2(center.x + glyph, center.y - glyph), ImVec2(center.x + glyph, center.y + glyph - offset), color);
			draw.AddLine(ImVec2(center.x + glyph, center.y + glyph - offset), ImVec2(center.x + glyph - offset, center.y + glyph - offset), color);
		}
		else draw.AddRect(ImVec2(center.x - glyph, center.y - glyph), ImVec2(center.x + glyph, center.y + glyph), color);

		const bool close = button(2, "##Close", close_hovered, close_pressed);

		if (ImGui::IsItemHovered())
			color = close_glyph;

		draw.AddLine(ImVec2(center.x - glyph, center.y - glyph), ImVec2(center.x + glyph, center.y + glyph), color);
		draw.AddLine(ImVec2(center.x - glyph, center.y + glyph), ImVec2(center.x + glyph, center.y - glyph), color);

		draw.PopClipRect();

		return close;
	};

	void Editor::DrawTornBar(const TornWindows::Torn& torn)
	{
		DrawMenus();

		ImGuiViewport* const viewport = ImGui::GetWindowViewport();
		GLFWwindow* const window = (GLFWwindow*)viewport->PlatformHandle;

		// The OS window comes at the end of the first frame.
		if (window == nullptr)
			return;

		std::unique_ptr<WindowFrame>& frame = m_tornFrames[viewport->ID];

		if (frame == nullptr)
		{
			frame = std::make_unique<WindowFrame>();
			frame->Install(window, RedrawWhileHeld);
		};

		if (!DrawWindowButtons(window, *frame) && !viewport->PlatformRequestClose)
			return;

		const std::vector<ImGuiWindow*> docked = m_tornWindows.Docked(torn);

		const auto close = [&docked](const char* name, bool* shown)
		{
			const ImGuiID id = ImHashStr(name);

			for (const ImGuiWindow* panel : docked)
				if (panel->ID == id)
					*shown = false;
		};

		for (const Panel& panel : Panels())
			close(panel.title, panel.shown);

		close(EditorPanels::PrefabId, &m_showPrefab);
		close(EditorPanels::Settings, &m_showSettings);
		close(imgui_demo, &m_showImGuiDemo);
	};

	#pragma endregion

	#pragma region Screenshots

	void Editor::CaptureFrame()
	{
		if (m_screenshotWindow.empty())
			Engine::CaptureFrame(m_screenshotPath);
		else if (const std::optional<Engine::Region> region = UiScript::WindowRegion(m_screenshotWindow))
			Engine::CaptureFrame(m_screenshotPath, region);
		else std::cerr << "No window whose name contains \"" << m_screenshotWindow << "\" is showing" << std::endl;

		m_screenshotPath.clear();

		if (m_screenshotExits)
			Engine::Stop();
	};

	void Editor::RequestScreenshot(const std::string& path, int after_frames, bool exit_after, const std::string& window)
	{
		m_screenshotPath = path;
		m_screenshotWindow = window;
		m_screenshotFrame = after_frames;
		m_screenshotExits = exit_after;
		m_frame = 0;
	};

	#pragma endregion

	#pragma region Layouts

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
		ImGui::DockBuilderDockWindow(EditorPanels::Inspector, right);
		ImGui::DockBuilderDockWindow(EditorPanels::Console, bottom);
		ImGui::DockBuilderDockWindow(EditorPanels::SceneData, bottom);
		ImGui::DockBuilderDockWindow(EditorPanels::Stats, bottom);
		ImGui::DockBuilderDockWindow(EditorPanels::KeyBindings, bottom);
		ImGui::DockBuilderDockWindow(EditorPanels::BuildSettings, bottom);
		ImGui::DockBuilderDockWindow(EditorPanels::Tests, bottom);

		for (const ExtensionRegistry::Panel& panel : ExtensionRegistry::Panels())
			ImGui::DockBuilderDockWindow(panel.title.c_str(), right);

		for (const char* window : centre_windows)
			ImGui::DockBuilderDockWindow(window, centre);

		ImGui::DockBuilderFinish(dockspace_id);
	};

	std::vector<Editor::Panel> Editor::Panels()
	{
		std::vector<Panel> panels = {
			{ EditorPanels::Hierarchy, &m_showHierarchy, true },
			{ EditorPanels::Project, &m_showProject, true },
			{ EditorPanels::Inspector, &m_showInspector, true },
			{ EditorPanels::Game, &m_showGame, true },
			{ EditorPanels::Debug, &m_showDebug, true },
			{ EditorPanels::SceneData, &m_showSceneData, true },
			{ EditorPanels::Console, &m_showConsole, true },
			{ EditorPanels::Shaders, &m_showShaders, false },
			{ EditorPanels::Dialogues, &m_showDialogues, false },
			{ EditorPanels::Sprites, &m_showSprites, false },
			{ EditorPanels::Model, &m_showModels, false },
			{ EditorPanels::Palette, &m_showPalette, false },
			{ EditorPanels::Stats, &m_showStats, true },
			{ EditorPanels::KeyBindings, &m_showKeyBindings, true },
			{ EditorPanels::BuildSettings, &m_showBuildSettings, true },
			{ EditorPanels::Tests, &m_showTests, true } };

		for (ExtensionRegistry::Panel& panel : ExtensionRegistry::Panels())
			panels.push_back({ panel.title.c_str(), &panel.shown, false });

		return panels;
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
					? "Invalid name"
					: std::any_of(
						m_layouts.begin(),
						m_layouts.end(),
						[this](const std::string& layout) { return EditorLayouts::SameName(layout, m_layoutName); })
						? "Replaces existing"
						: "");

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

	#pragma endregion

	#pragma region Dialogs

	void Editor::DrawFolderPrompt()
	{
		if (!m_askForFolder)
			return;

		constexpr float folder_width = 460.0f;
		constexpr float button_width = 160.0f;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		// Not a modal, since the file browser it opens is one.
		if (ImGui::Begin(
			"Welcome to Loom Editor",
			nullptr,
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::SetNextItemWidth(folder_width);
			ImGui::InputText("##folder", m_folderBuffer, sizeof(m_folderBuffer));

			ImGui::SameLine();

			if (ImGui::Button("Browse..."))
				m_dialogs->folder.Open();

			if (ImGui::Button("Open Folder", ImVec2(button_width, 0.0f)))
				if (OpenProject(m_folderBuffer))
					m_askForFolder = false;

			ImGui::SameLine();

			if (ImGui::Button("Skip", ImVec2(button_width, 0.0f)))
			{
				if (Scene::GetScenes().empty())
					CreateScene("Untitled Scene");

				m_askForFolder = false;
			};
		};

		ImGui::End();
	};

	void Editor::LeaveFor(const std::string& project)
	{
		Stop();

		AskToSave(
			UnsavedWork(),
			[this]() { return SaveProject(); },
			[this, project]()
			{
				if (OpenProject(project))
					m_askForFolder = false;
			});
	};

	void Editor::AskToSave(
		std::vector<std::string> unsaved,
		std::function<bool()> save,
		std::function<void()> then)
	{
		if (m_saveQuestion)
			return;

		if (unsaved.empty())
			then();
		else m_saveQuestion = SaveQuestion{ std::move(unsaved), std::move(save), std::move(then) };
	};

	void Editor::DrawSaveQuestion()
	{
		constexpr const char* popup = "Unsaved Changes";

		if (m_saveQuestion && !ImGui::IsPopupOpen(popup))
			ImGui::OpenPopup(popup);

		if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		if (!m_saveQuestion)
		{
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		};

		ImGui::TextUnformatted("Save the changes to:");

		for (const std::string& name : m_saveQuestion->unsaved)
			ImGui::BulletText("%s", name.c_str());

		const bool save = ImGui::Button("Save");
		ImGui::SameLine();
		const bool discard = ImGui::Button("Don't Save");
		ImGui::SameLine();
		const bool cancel = ImGui::Button("Cancel");

		const bool saved = save && m_saveQuestion->save();

		if (saved)
			RefreshProjectAssets();

		if (saved || discard || cancel)
		{
			const SaveQuestion question = std::move(*std::exchange(m_saveQuestion, std::nullopt));

			ImGui::CloseCurrentPopup();

			if (!cancel)
				question.then();
		};

		ImGui::EndPopup();
	};

	void Editor::ShowOpenProjectDialog()
	{
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
			LeaveFor(m_dialogs->folder.GetSelected().string());
			m_dialogs->folder.ClearSelected();
		};

		m_dialogs->openProject.Display();

		if (m_dialogs->openProject.HasSelected())
		{
			LeaveFor(m_dialogs->openProject.GetSelected().string());
			m_dialogs->openProject.ClearSelected();
		};

		m_dialogs->buildFolder.Display();

		if (m_dialogs->buildFolder.HasSelected())
		{
			m_buildSettings.folder = m_dialogs->buildFolder.GetSelected().string();
			m_dialogs->buildFolder.ClearSelected();
			m_buildSettings.Keep();
		};
	};

	#pragma endregion

	#pragma region Hierarchy

	#pragma region Nodes

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

			// A copy, since closing a scene from a context menu edits the engine's list.
			const std::vector<Scene*> scenes = Scene::GetScenes();

			GatherOpenNodes();

			const bool root = m_selected && m_selected->GetParent() == nullptr;

			// Focus moving into the rename field is not the selection moving.
			const bool focused_rename = m_renameFieldId != 0 && ImGui::GetCurrentContext()->NavJustMovedToId == m_renameFieldId;

			ImGui::BeginMultiSelect(
				focused_rename ? ImGuiMultiSelectFlags_NoAutoClear : ImGuiMultiSelectFlags_None,
				root ? 0 : (int)m_selection.size(),
				-1);

			// A selection made elsewhere is where a Shift+click range starts.
			if (m_rangeSourceFor != m_selectedId)
				ImGui::GetCurrentContext()->CurrentMultiSelect->Storage->RangeSrcItem = root || m_selected == nullptr
					? ImGuiSelectionUserData_Invalid
					: (ImGuiSelectionUserData)m_selectedId;

			ApplySelection(ImGui::GetCurrentContext()->CurrentMultiSelect->IO);

			// Tree ids are GUIDs, which a scene opened twice shares with its copy.
			std::unordered_map<Guid, int> copies{ };

			for (Scene* scene : scenes)
			{
				ImGui::PushID(copies[scene->GetGuid()]++);
				DrawSceneNode(scene);
				ImGui::PopID();
			};

			DrawPrefabNode();

			ApplySelection(*ImGui::EndMultiSelect());
			m_rangeSourceFor = m_selectedId;

			GameObject* last_rows = GetPrefabRoot();

			if (last_rows == nullptr && m_activeScene)
				last_rows = &m_activeScene->GetRoot();

			const ImGuiWindow* window = ImGui::GetCurrentWindow();
			const ImRect below(
				window->InnerRect.Min.x,
				(std::max)(ImGui::GetCursorScreenPos().y, window->InnerRect.Min.y),
				window->InnerRect.Max.x,
				window->InnerRect.Max.y);

			if (last_rows && below.GetHeight() > 0.0f && ImGui::BeginDragDropTargetCustom(below, window->ID))
			{
				if (DropPlaced(*last_rows, true, Hierarchy::Zone::Into))
					window->DrawList->AddLine(
						below.Min,
						ImVec2(below.Max.x, below.Min.y),
						ImGui::GetColorU32(ImGuiCol_DragDropTarget));

				ImGui::EndDragDropTarget();
			};

			if (ImGui::BeginPopupContextWindow("##create", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
			{
				if (ImGui::MenuItem("Add GameObject", nullptr, false, last_rows != nullptr))
					Select(last_rows->AddChild());

				DrawShapeMenu(last_rows);
				DrawCutsceneMenu(last_rows);
				DrawRenderingMenu(last_rows);

				ImGui::EndPopup();
			};

			m_childCounts.swap(m_drawnCounts);
			m_drawnCounts.clear();
		};

		ImGui::End();
	};

	void Editor::DrawSceneNode(Scene* scene)
	{
		GameObject& root = scene->GetRoot();

		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_AllowOverlap;

		if (m_selected == &root)
			flags |= ImGuiTreeNodeFlags_Selected;

		const bool is_active = scene == m_activeScene;
		const bool styled = is_active || root.IsDisabled();

		if (styled)
			ImGui::PushStyleColor(
				ImGuiCol_Text,
				root.IsDisabled() ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : EditorTheme::AccentText);

		const bool was_open = NodeOpen(root);
		ImGui::SetNextItemOpen(was_open);

		const ImRect eye = EyeRect();

		const bool open = ImGui::TreeNodeEx(
			scene->GetGuid().ToString().c_str(),
			flags,
			"%s %s%s%s",
			EditorIcons::Scene,
			scene->NameAndID().c_str(),
			IsOwned(scene) && SceneUnsaved(scene) ? " *" : "",
			is_active ? "  [active]" : "");

		if (open != was_open)
			SetNodeOpen(root, open);

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
			if (ImGui::MenuItem("Add GameObject"))
				Select(scene->AddChild());

			DrawShapeMenu(&root);
			DrawCutsceneMenu(&root);
			DrawRenderingMenu(&root);

			ImGui::Separator();

			if (ImGui::MenuItem("Close Scene", nullptr, false, IsOwned(scene) && !m_playing))
				AskToSave(
					SceneUnsaved(scene) ? std::vector<std::string>{ scene->GetName() } : std::vector<std::string>{ },
					[this, scene]() { return !IsOwned(scene) || SaveToProject(scene); },
					[this, scene]()
					{
						if (IsOwned(scene))
							CloseScene(scene);
					});

			ImGui::EndPopup();
		};

		DrawEnableToggle(&root, false, eye);

		if (!open)
			return;

		DrawChildNodes(root, root.IsDisabled(), false);

		ImGui::TreePop();
	};

	void Editor::DrawGameObjectNode(GameObject* gameObject, bool parent_disabled, bool parent_in_prefab)
	{
		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_AllowOverlap;

		const bool leaf = gameObject->GetChildren().empty();

		if (leaf)
			flags |= ImGuiTreeNodeFlags_Leaf;

		if (IsSelected(*gameObject))
			flags |= ImGuiTreeNodeFlags_Selected;

		const bool was_open = NodeOpen(*gameObject);
		ImGui::SetNextItemOpen(was_open);

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

		ImGui::SetNextItemSelectionUserData((ImGuiSelectionUserData)gameObject->m_ID);

		const bool open = ImGui::TreeNodeEx(
			gameObject->GetGuid().ToString().c_str(),
			flags,
			"%s",
			label.c_str());

		// A leaf draws open whatever it is set to, so it keeps what it was.
		if (!leaf && open != was_open)
			SetNodeOpen(*gameObject, open);

		if (disabled || in_prefab)
			ImGui::PopStyleColor();

		TakeImportTarget(gameObject);
		DragAndDropNode(gameObject, open);

		DragSource(*gameObject);

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
			if (!IsSelected(*gameObject))
				Select(gameObject);

			if (ImGui::MenuItem("Add Child"))
				Select(gameObject->AddChild());

			DrawShapeMenu(gameObject);
			DrawCutsceneMenu(gameObject);
			DrawRenderingMenu(gameObject);

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

		DrawChildNodes(*gameObject, disabled, in_prefab);

		ImGui::TreePop();
	};

	void Editor::DrawShapeMenu(GameObject* parent)
	{
		if (!ImGui::BeginMenu("3D Object", parent != nullptr))
			return;

		for (const Shapes::Info& info : Shapes::All())
			if (ImGui::MenuItem(info.label))
				Select(Shapes::Add(*parent, info.shape, ShapeShader()));

		ImGui::EndMenu();
	};

	void Editor::DrawCutsceneMenu(GameObject* parent)
	{
		if (!ImGui::BeginMenu("Cutscene", parent != nullptr))
			return;

		if (ImGui::MenuItem(Cutscenes::DialogueBoxLabel))
			Select(Cutscenes::AddDialogueBox(*parent, FirstFont()));

		ImGui::EndMenu();
	};

	void Editor::DrawRenderingMenu(GameObject* parent)
	{
		if (!ImGui::BeginMenu("Rendering", parent != nullptr))
			return;

		if (ImGui::MenuItem(RenderingObjects::PreVertexLabel))
			Select(RenderingObjects::AddPreVertex(*parent, FirstPreVertex()));

		if (ImGui::MenuItem(RenderingObjects::PixelShaderLabel))
			Select(RenderingObjects::AddPixelShader(*parent));

		if (ImGui::MenuItem(RenderingObjects::LightLabel))
			Select(RenderingObjects::AddLight(*parent));

		if (ImGui::MenuItem(RenderingObjects::CameraLabel))
			Select(RenderingObjects::AddCamera(*parent));

		if (ImGui::MenuItem(RenderingObjects::ComputeShaderLabel))
			Select(RenderingObjects::AddComputeShader(*parent));

		ImGui::EndMenu();
	};

	std::string Editor::FirstFont() const
	{
		for (const FileTab& tab : m_fileTabs)
			if (&tab.kind == &FileTabs::Fonts && !tab.files.empty())
				return *std::min_element(tab.files.begin(), tab.files.end());

		return "";
	};

	std::string Editor::FirstPreVertex() const
	{
		if (m_projectPreVertices.empty())
			return "";

		return ProjectAssets::RelativeTo(*std::min_element(m_projectPreVertices.begin(), m_projectPreVertices.end()), m_projectPath);
	};

	std::string Editor::ShapeShader() const
	{
		return m_projectPath.empty() ? "" : ProjectAssets::ProjectShader(m_projectPath);
	};

	#pragma endregion

	#pragma region Open Rows

	void Editor::DrawChildNodes(GameObject& parent, bool disabled, bool in_prefab)
	{
		const ImRect& clip = ImGui::GetCurrentWindow()->ClipRect;
		const auto open = m_openUnder.find(&parent);

		const Hierarchy::View view
		{
			ImGui::GetCursorScreenPos().y,
			ImGui::GetTextLineHeightWithSpacing(),
			clip.Min.y,
			clip.Max.y
		};

		// Drawn wherever it is, or its field lets go of the keyboard.
		const std::vector<Hierarchy::Step> steps = Hierarchy::Plan(
			parent,
			open == m_openUnder.end() ? nullptr : &open->second,
			m_rowsBelow,
			view,
			m_renaming ? m_selected : nullptr);

		for (const Hierarchy::Step& step : steps)
		{
			SkipRows(step.skip);

			if (step.child)
				DrawGameObjectNode(step.child, disabled, in_prefab);
		};
	};

	void Editor::SkipRows(size_t rows)
	{
		if (rows == 0)
			return;

		// The spacing after the last row is the one Dummy adds itself.
		ImGui::Dummy(ImVec2(0.0f, rows * ImGui::GetTextLineHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y));
	};

	void Editor::GatherOpenNodes()
	{
		std::unordered_map<uint64_t, Guid> open{ };

		m_openUnder.clear();

		for (const auto& [id, guid] : m_openNodes)
		{
			const GameObject* node = dynamic_cast<const GameObject*>(LoomObject::GetByID(id));

			if (node == nullptr)
				node = LoomObject::GetByGuid<GameObject>(guid);

			// Kept while it is gone, so the undo that brings it back has it open.
			if (node == nullptr)
			{
				open.emplace(id, guid);
				continue;
			};

			if (open.emplace(node->m_ID, guid).second && node->GetParent())
				m_openUnder[node->GetParent()].emplace_back(node->GetSiblingIndex(), node);
		};

		m_openNodes = std::move(open);
		m_rowsBelow = Hierarchy::CountRowsBelow(m_openUnder);
	};

	bool Editor::NodeOpen(const GameObject& node)
	{
		const size_t count = node.GetChildren().size();

		if (const auto last = m_childCounts.find(node.m_ID); last != m_childCounts.end() && count > last->second)
			SetNodeOpen(node, true);

		m_drawnCounts[node.m_ID] = count;

		return ShownOpen(node);
	};

	bool Editor::ShownOpen(const GameObject& node) const
	{
		return node.GetParent() == nullptr
			? !m_closedRoots.contains(node.m_ID)
			: m_openNodes.contains(node.m_ID);
	};

	void Editor::SetNodeOpen(const GameObject& node, bool open)
	{
		if (node.GetParent() == nullptr)
		{
			if (open)
				m_closedRoots.erase(node.m_ID);
			else
				m_closedRoots.insert(node.m_ID);
		}
		else if (open)
			m_openNodes.emplace(node.m_ID, node.GetGuid());
		else
			m_openNodes.erase(node.m_ID);
	};

	#pragma endregion

	#pragma region Row Input

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

	void Editor::DragAndDropNode(GameObject* gameObject, bool open)
	{
		if (!ImGui::BeginDragDropTarget())
			return;

		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		// Beside a prefab's root is outside the prefab, where nothing draws it.
		const bool root = gameObject->GetParent() == nullptr || gameObject == GetPrefabRoot();
		const Hierarchy::Zone zone = Hierarchy::ZoneOf(min.y, max.y, ImGui::GetMousePos().y, root);

		if (!DropScript(gameObject) && DropPlaced(*gameObject, open, zone))
		{
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImU32 colour = ImGui::GetColorU32(ImGuiCol_DragDropTarget);

			switch (zone)
			{
			case Hierarchy::Zone::Above:	draw->AddLine(min, ImVec2(max.x, min.y), colour);	break;
			case Hierarchy::Zone::Into:		draw->AddRect(min, max, colour);					break;
			case Hierarchy::Zone::Below:	draw->AddLine(ImVec2(min.x, max.y), max, colour);	break;
			};
		};

		ImGui::EndDragDropTarget();
	};

	bool Editor::DropPlaced(GameObject& row, bool open, Hierarchy::Zone zone)
	{
		const ImGuiPayload* dragged = ImGui::GetDragDropPayload();

		if (dragged == nullptr)
			return false;

		constexpr ImGuiDragDropFlags flags =
			ImGuiDragDropFlags_AcceptBeforeDelivery |
			ImGuiDragDropFlags_AcceptNoDrawDefaultRect;

		if (dragged->IsDataType(ASSET_PAYLOAD))
		{
			const std::string path = (const char*)dragged->Data;
			const Hierarchy::Placement placement = Hierarchy::Place(row, open, zone, nullptr);

			if (!CanPlace(path, *placement.parent))
				return false;

			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ASSET_PAYLOAD, flags);

			if (payload && payload->IsDelivery())
				PlaceAsset(path, placement);

			return payload != nullptr;
		};

		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(object_payload, flags);

		GameObject* moving = payload
			? LoomObject::GetByGuid<GameObject>(*(const Guid*)payload->Data)
			: nullptr;

		if (moving == nullptr || !EditCommands::CanMove(*moving, row, m_ownedScenes))
			return false;

		if (payload->IsDelivery())
		{
			const Hierarchy::Placement placement = Hierarchy::Place(row, open, zone, moving);

			moving->SetParent(placement.parent, placement.before);
			Select(moving);

			m_historyDirty = true;
		};

		return true;
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

		m_renameFieldId = ImGui::GetID("##rename");

		ImGui::InputText(
			"##rename",
			m_nameBuffer,
			sizeof(m_nameBuffer),
			ImGuiInputTextFlags_AutoSelectAll);

		// Escape has already restored the old name by the time the field lets go.
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

	#pragma endregion

	#pragma endregion

	#pragma region Asset Drops

	bool Editor::CanPlace(const std::string& path, const GameObject& parent) const
	{
		// A prefab inside itself would take in its own every edit.
		if (std::filesystem::path(path).extension() == Prefab::extension)
			return !IsInPrefab(&parent, path);

		return
			IsModelFile(path) ||
			IsSpriteSource(path) ||
			ProjectAssets::IsSoundFile(path);
	};

	GameObject* Editor::PlaceAsset(const std::string& path, const Hierarchy::Placement& placement)
	{
		const std::string name = std::filesystem::path(path).stem().string();

		GameObject* placed = nullptr;

		if (std::filesystem::path(path).extension() == Prefab::extension)
			placed = Instantiate(path, *placement.parent);
		else if (IsModelFile(path))
			placed = ImportModel(path, *placement.parent);
		else if (IsSpriteSource(path))
		{
			placed = placement.parent->AddChild(name);
			placed->Attach<Sprite>()->texturePath = SpriteImage(path);
		}
		else
		{
			placed = placement.parent->AddChild(name);
			placed->Attach<AudioSource>()->clipPath = path;
		};

		if (placed == nullptr)
			return nullptr;

		placed->SetParent(placement.parent, placement.before);
		Select(placed);

		m_historyDirty = true;

		return placed;
	};

	bool Editor::AcceptScriptsDrop(std::string& path, ScriptEntryKind kind)
	{
		if (!ImGui::BeginDragDropTarget())
			return false;

		const ImGuiPayload* dragged = ImGui::GetDragDropPayload();
		bool dropped = false;

		if (dragged != nullptr && dragged->IsDataType(script_payload))
			if (const auto entry = m_scriptEntries.find((const char*)dragged->Data);
				entry != m_scriptEntries.end() &&
				entry->second.kind == kind &&
				ImGui::AcceptDragDropPayload(script_payload))
			{
				path = entry->first;
				dropped = true;
			};

		ImGui::EndDragDropTarget();

		return dropped;
	};

	void Editor::DropShader(GameObject* gameObject, const std::string& path)
	{
		ChangeComponents(
			*gameObject,
			[gameObject, &path]()
			{
				Material* material = gameObject->GetComponent<Material>();

				if (material == nullptr)
					material = gameObject->Attach<Material>();

				material->ChangeShader(path);
			});

		Select(gameObject);
		m_historyDirty = true;
	};

	void Editor::DropPixelShader(GameObject* gameObject, const std::string& path)
	{
		ChangeComponents(
			*gameObject,
			[gameObject, &path]()
			{
				PixelShader* pixel = gameObject->GetComponent<PixelShader>();

				if (pixel == nullptr)
					pixel = gameObject->Attach<PixelShader>();

				pixel->ChangeShader(path);
			});

		Select(gameObject);
		m_historyDirty = true;
	};

	void Editor::DropComputeShader(GameObject* gameObject, const std::string& path)
	{
		ChangeComponents(
			*gameObject,
			[gameObject, &path]()
			{
				ComputeShader* compute = gameObject->GetComponent<ComputeShader>();

				if (compute == nullptr)
					compute = gameObject->Attach<ComputeShader>();

				compute->ChangeShader(path);
			});

		Select(gameObject);
		m_historyDirty = true;
	};

	void Editor::DropPreVertex(GameObject* gameObject, const std::string& path)
	{
		ChangeComponents(
			*gameObject,
			[gameObject, &path]()
			{
				PreVertex* preVertex = gameObject->GetComponent<PreVertex>();

				if (preVertex == nullptr)
					preVertex = gameObject->Attach<PreVertex>();

				preVertex->asset = path;
				PreVertex::ReloadIfChanged();
			});

		Select(gameObject);
		m_historyDirty = true;
	};

	bool Editor::DropScript(GameObject* gameObject)
	{
		const ImGuiPayload* dragged = ImGui::GetDragDropPayload();

		if (dragged == nullptr || !dragged->IsDataType(script_payload))
			return false;

		const std::string name = (const char*)dragged->Data;

		const auto entry = m_scriptEntries.find(name);

		if (entry != m_scriptEntries.end() && (entry->second.kind == ScriptEntryKind::State || entry->second.kind == ScriptEntryKind::Dialogue))
			return true;

		if (entry != m_scriptEntries.end() && entry->second.kind == ScriptEntryKind::Shader)
		{
			if (ImGui::AcceptDragDropPayload(script_payload))
				DropShader(gameObject, name);

			return true;
		};

		if (entry != m_scriptEntries.end() && entry->second.kind == ScriptEntryKind::PixelShader)
		{
			if (ImGui::AcceptDragDropPayload(script_payload))
				DropPixelShader(gameObject, name);

			return true;
		};

		if (entry != m_scriptEntries.end() && entry->second.kind == ScriptEntryKind::ComputeShader)
		{
			if (ImGui::AcceptDragDropPayload(script_payload))
				DropComputeShader(gameObject, name);

			return true;
		};

		if (entry != m_scriptEntries.end() && entry->second.kind == ScriptEntryKind::PreVertex)
		{
			if (ImGui::AcceptDragDropPayload(script_payload))
				DropPreVertex(gameObject, name);

			return true;
		};

		if (!ImGui::AcceptDragDropPayload(script_payload))
			return true;

		bool made = false;

		ChangeComponents(*gameObject, [&]() { made = ComponentRegistry::Create(name, *gameObject) != nullptr; });

		if (!made)
		{
			std::cerr << name << " is not a component the scripts have compiled. One written since the last compile attaches after the next." << std::endl;
			return true;
		};

		Select(gameObject);
		m_historyDirty = true;

		return true;
	};

	#pragma endregion

	#pragma region Reloads

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

		const std::vector<std::pair<std::string, std::string>> renamed = m_sprites.TakeRenamed();

		for (const auto& [from, to] : renamed)
		{
			Refile(from, to);
			m_selectedAsset = ProjectAssets::Relocate(m_selectedAsset, from, to);
		};

		if (!renamed.empty())
			RefreshProjectAssets();
	};

	void Editor::TakeSavedModels()
	{
		// The next pass over the prefabs finds the file newer and hands it on.
		if (!m_models.TakeSaved().empty())
			m_historyDirty = true;
	};

	#pragma endregion

	#pragma region Inspector

	#pragma region Panel

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
				const std::vector<GameObject*> objects = GetSelection();
				GameObject* gameObject = objects.front();

				const bool mixed_name = std::any_of(
					objects.begin() + 1,
					objects.end(),
					[gameObject](GameObject* other) { return other->GetName() != gameObject->GetName(); });

				// Only refilled on a new selection or rename, so typing does not fight the name.
				if (m_nameBufferOwner != gameObject)
				{
					snprintf(
						m_nameBuffer,
						sizeof(m_nameBuffer),
						"%s",
						mixed_name ? "" : gameObject->GetName().c_str());

					m_nameBufferOwner = gameObject;
				};

				if (objects.size() > 1)
					ImGui::TextDisabled("%d objects", (int)objects.size());
				else
				{
					ImGui::TextDisabled("%s", gameObject->GetGuid().ToString().c_str());

					if (ImGui::IsItemClicked())
						ImGui::SetClipboardText(gameObject->GetGuid().ToString().c_str());

					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Click to copy");
				};

				constexpr const char* name_label = "Name";

				const float column = LabelColumn(
					FieldNames::Of(*gameObject),
					ImGui::CalcTextSize(name_label).x);

				Label(name_label, column);
				ImGui::PushItemWidth(-FLT_MIN);

				if (ImGui::InputTextWithHint(
					"##name",
					mixed_name ? MIXED_VALUE : "",
					m_nameBuffer,
					sizeof(m_nameBuffer),
					ImGuiInputTextFlags_EnterReturnsTrue) &&
					!(mixed_name && m_nameBuffer[0] == '\0'))
					for (GameObject* each : objects)
						each->SetName(m_nameBuffer);

				ImGui::PopItemWidth();

				DrawTransform(objects, column);
				DrawThread(objects, column);

				ImGui::Separator();

				const std::vector<std::vector<ComponentBase*>> components = MultiEdit::Common(objects);

				for (const std::vector<ComponentBase*>& matches : components)
					DrawComponent(objects, matches);

				if (DraggedComponent(*gameObject) != nullptr)
					ScrollWhileReordering();

				if (!components.empty())
					ImGui::Separator();

				if (ImGui::Button("Add Component", ImVec2(-FLT_MIN, 0.0f)))
					ImGui::OpenPopup("##add_component");

				// Not while appearing, since docking closes a popup opened then.
				if (m_openAddComponent && !ImGui::IsWindowAppearing())
				{
					ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
					ImGui::OpenPopup("##add_component");
					m_openAddComponent = false;
				};

				if (ImGui::BeginPopup("##add_component"))
				{
					if (!m_projectPath.empty())
					{
						if (ImGui::MenuItem((std::string(EditorIcons::New) + " New").c_str()))
							AskForScriptOn(AssetKind::Script, objects);

						ImGui::Separator();
					};

					if (ComponentRegistry::All().empty())
						ImGui::TextDisabled("Nothing registered.");

					DrawComponentMenu(objects, m_componentMenu.Build(m_scripts.GetTypes()));

					ImGui::EndPopup();
				};
			};
		};

		ImGui::End();
	};

	#pragma endregion

	#pragma region Add Component

	void Editor::AskForScriptOn(AssetKind kind, const std::vector<GameObject*>& objects)
	{
		AskForAsset(kind, m_projectPath);

		for (const GameObject* gameObject : objects)
			m_newScriptFor.push_back(gameObject->GetGuid());
	};

	void Editor::DrawComponentMenu(const std::vector<GameObject*>& objects, const ComponentMenu::Node& node)
	{
		for (const ComponentMenu::Node& child : node.children)
			if (ImGui::BeginMenu(child.name.c_str()))
			{
				DrawComponentMenu(objects, child);
				ImGui::EndMenu();
			};

		if (!node.children.empty() && !node.components.empty())
			ImGui::Separator();

		// Each is a base, so picking one makes a script of that kind, which needs a project.
		constexpr std::pair<std::string_view, AssetKind> derived[] =
		{
			{ "StateMachine", AssetKind::StateMachine },
			{ "DialogueManager", AssetKind::DialogueManager },
		};

		for (const std::string& component : node.components)
		{
			if (ImGui::MenuItem(component.c_str()))
			{
				const auto base = std::find_if(std::begin(derived), std::end(derived), [&component](const auto& kind) { return kind.first == component; });

				if (base != std::end(derived) && !m_projectPath.empty())
					AskForScriptOn(base->second, objects);
				else ChangeComponents(objects, [&](size_t i) { ComponentRegistry::Create(component, *objects[i]); });
			};

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("Right-click: category");

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
			ImGui::TextDisabled("No project open");

		ImGui::EndPopup();
	};

	void Editor::SetComponentCategory(const std::string& component, const std::string& category)
	{
		m_componentMenu.Set(component, category);

		if (!m_projectFile.empty() && !m_componentMenu.Save(m_projectFile))
			std::cerr << "Could not write the component categories to " << m_projectFile << std::endl;
	};

	#pragma endregion

	#pragma region Source

	Editor::SourceLine Editor::FindSource(const std::string& component) const
	{
		std::vector<std::string> files{ };

		if (const auto entry = m_scriptEntries.find(component); entry != m_scriptEntries.end() && !entry->second.file.empty())
			files.push_back(entry->second.file);

		std::error_code code;

		for (auto entry = std::filesystem::recursive_directory_iterator(std::filesystem::path(ProjectTemplate::EngineRoot()) / "Loom WASM", code);
			entry != std::filesystem::recursive_directory_iterator();
			entry.increment(code))
			if (ProjectAssets::IsScriptFile(entry->path().filename().string()))
				files.push_back(entry->path().string());

		for (const std::string& file : files)
			if (const int line = ProjectAssets::DeclarationLine(ProjectAssets::ReadText(file), component))
				return SourceLine{ file, line };

		return { };
	};

	void Editor::AddDialogueBinding(const std::string& type)
	{
		const SourceLine source = FindSource(type);
		const std::string scripts = std::filesystem::path(ScriptsProject()).parent_path().string();

		if (source.path.empty() || !ProjectAssets::IsUnder(source.path, scripts))
		{
			std::cerr << "No script under " << scripts << " defines " << type << '.' << std::endl;
			return;
		};

		int line = 0;
		std::string error;
		const std::string text = ProjectAssets::WithDialogueBinding(ProjectAssets::ReadText(source.path), source.line, &line);

		if (text.empty())
			std::cerr << type << " in " << source.path << " never closes its braces." << std::endl;
		else if (!ProjectAssets::WriteText(source.path, text, &error))
			std::cerr << "Could not add a binding to " << type << ": " << error << std::endl;
		else OpenInVisualStudio(source.path, line);
	};

	void Editor::DrawSourcePopup(const std::string& component)
	{
		if (!ImGui::BeginPopupContextItem())
			return;

		if (ImGui::IsWindowAppearing())
			m_componentSource = FindSource(component);

		const bool found = !m_componentSource.path.empty();

		const std::string where = found
			? std::filesystem::path(m_componentSource.path).filename().string() + ':' + std::to_string(m_componentSource.line)
			: "";

		if (ImGui::MenuItem("Go to Source", where.c_str(), false, found))
			OpenInVisualStudio(m_componentSource.path, m_componentSource.line);

		ImGui::EndPopup();
	};

	#pragma endregion

	#pragma region Model Preview

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
			ImGui::TextWrapped("Unreadable model");
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

	#pragma endregion

	#pragma region Transform

	void Editor::DrawTransform(const std::vector<GameObject*>& objects, float column)
	{
		GameObject* gameObject = objects.front();
		const bool open = ComponentHeader("Transform", *gameObject);

		DrawSourcePopup("Transform");

		if (!open)
			return;

		const std::vector<LoomObject*> held(objects.begin(), objects.end());

		const auto row =
			[this, gameObject, &held, column](const char* name, Serial<glm::vec3>& serial, float speed)
			{
				ImGui::PushID(name);
				Label(name, column);

				const glm::vec3 before = serial;
				glm::vec3 value = serial;

				const size_t index = FieldAt(*gameObject, &(const glm::vec3&)serial);
				const MultiEdit::Axes mixed = MultiEdit::MixedAxes(held, index);
				const std::vector<std::string> written = m_playing ? Written(held, index) : std::vector<std::string>();

				bool dragged = false;

				if (AxisGui::DragFloatN("##value", glm::value_ptr(value), 3, mixed.data(), speed, float_format, &dragged))
				{
					serial = value;

					if (dragged)
						MultiEdit::Shift(held, index, glm::value_ptr(before));
					else
						MultiEdit::Spread(held, index, glm::value_ptr(before));

					if (m_playing)
						TrackPlayEdit(held, index, written);
				};

				ImGui::PopID();
			};

		ImGui::PushItemWidth(-FLT_MIN);

		row("Position", gameObject->transform.position, drag_speed);
		row("Rotation", gameObject->transform.rotation, rotation_drag_speed);
		row("Scale", gameObject->transform.scale, drag_speed);

		ImGui::PopItemWidth();
	};

	void Editor::DrawThread(const std::vector<GameObject*>& objects, float column)
	{
		GameObject* gameObject = objects.front();

		if (!ComponentHeader("Thread", *gameObject))
			return;

		ImGui::PushItemWidth(-FLT_MIN);

		bool inherit = gameObject->InheritsThreadID();
		int thread = gameObject->GetThreadID();

		const bool mixed_inherit = std::any_of(
			objects.begin() + 1,
			objects.end(),
			[inherit](const GameObject* other) { return other->InheritsThreadID() != inherit; });

		const bool mixed_thread = std::any_of(
			objects.begin() + 1,
			objects.end(),
			[thread](const GameObject* other) { return other->GetThreadID() != thread; });

		Label("Inherit Parent", column);

		ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed_inherit);

		if (ImGui::Checkbox("##inherit", &inherit))
			for (GameObject* each : objects)
				each->SetInheritThreadID(inherit);

		ImGui::PopItemFlag();

		ImGui::BeginDisabled(inherit && !mixed_inherit);

		Label("Thread ID", column);

		if (ImGui::SliderInt(
			"##thread",
			&thread,
			GameObject::Unprocessed,
			(int)Jobs::Cores(),
			mixed_thread ? MIXED_VALUE : "%d"))
			for (GameObject* each : objects)
				each->SetThreadID(thread);

		ImGui::EndDisabled();

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%d: none, 0: main thread", GameObject::Unprocessed);

		ImGui::PopItemWidth();
	};

	#pragma endregion

	#pragma region Components

	void Editor::DrawComponent(const std::vector<GameObject*>& objects, const std::vector<ComponentBase*>& components)
	{
		GameObject* gameObject = objects.front();
		ComponentBase* component = components.front();

		// What a component draws for itself, and reordering, are for one at a time.
		const bool alone = objects.size() == 1;

		ImGui::PushID(component);

		const std::string name = ComponentRegistry::NameOf(*component);

		const bool open = ComponentHeader(name.c_str(), *gameObject);
		const ImRect header(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

		// Halfway into the gaps on each side, so neighbours' drop areas meet at the line.
		constexpr float half = 0.5f;
		const float gap = ImGui::GetStyle().ItemSpacing.y * half;
		const auto drop = [&]()
		{
			const ImRect area(header.Min.x, header.Min.y - gap, header.Max.x, ImGui::GetCursorScreenPos().y - gap);

			DropComponent(*gameObject, *component, area);
		};

		// Headers passed over neither light up nor fold.
		if (alone)
			DragSource(*component, ImGuiDragDropFlags_SourceNoHoldToOpenOthers);

		DrawSourcePopup(name);

		// The remove button rides on the header's own line, Unity-style.
		ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
		if (ImGui::SmallButton("x"))
			ChangeComponents(objects, [&](size_t i) { objects[i]->DetachComponent(components[i]); });

		if (!open)
		{
			drop();
			ImGui::PopID();
			return;
		};

		const std::vector<std::string> labels = FieldNames::Of(*component);

		DrawFields(std::vector<LoomObject*>(components.begin(), components.end()), labels, FieldLabelColumn(*component, labels));

		if (!alone)
		{
			ImGui::PopID();
			return;
		};

		if (const MissingComponent* missing = dynamic_cast<const MissingComponent*>(component))
			DrawKept(*missing);

		// A dialogue's states are its own, and its OnGui has New Binding instead.
		StateMachineBase* machine = dynamic_cast<StateMachineBase*>(component);

		if (machine != nullptr && dynamic_cast<DialogueManagerBase*>(component) == nullptr)
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

		drop();
		ImGui::PopID();
	};

	void Editor::ScrollWhileReordering()
	{
		const ComponentDrag::Scroll scroll = ComponentDrag::Step(
			ImGui::GetMousePos(),
			ImGui::GetCurrentWindow()->InnerRect,
			ImGui::GetFrameHeightWithSpacing(),
			ImGui::GetIO().DeltaTime,
			m_reorderScroll);

		m_reorderScroll = scroll.carried;

		if (scroll.pixels != 0.0f)
			ImGui::SetScrollY(std::clamp(ImGui::GetScrollY() + scroll.pixels, 0.0f, ImGui::GetScrollMaxY()));
	};

	void Editor::DropComponent(GameObject& gameObject, ComponentBase& onto, const ImRect& area)
	{
		ComponentBase* dragged = DraggedComponent(gameObject);

		if (dragged == nullptr || dragged == &onto)
			return;

		std::vector<ComponentBase*> order = gameObject.GetComponents();
		const auto from = std::find(order.begin(), order.end(), dragged);
		const bool below = ImGui::GetMousePos().y > area.GetCenter().y;

		// Just above or below where it already is would leave it there.
		const ptrdiff_t at = std::find(order.begin(), order.end(), &onto) - order.begin();

		if (at + (below ? 1 : -1) == from - order.begin())
			return;

		if (!ImGui::BeginDragDropTargetCustom(area, ImGui::GetID("##drop")))
			return;

		constexpr ImGuiDragDropFlags flags =
			ImGuiDragDropFlags_AcceptBeforeDelivery |
			ImGuiDragDropFlags_AcceptNoDrawDefaultRect;

		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(object_payload, flags))
		{
			const float y = below ? area.Max.y : area.Min.y;

			ImGui::GetWindowDrawList()->AddLine(ImVec2(area.Min.x, y), ImVec2(area.Max.x, y), ImGui::GetColorU32(ImGuiCol_DragDropTarget));

			if (payload->IsDelivery())
			{
				order.erase(from);
				order.insert(std::find(order.begin(), order.end(), &onto) + (below ? 1 : 0), dragged);

				ChangeComponents(gameObject, [&gameObject, &order]() { gameObject.OrderComponents(order); });
				m_historyDirty = true;
			};
		};

		ImGui::EndDragDropTarget();
	};

	#pragma endregion

	#pragma region Fields

	void Editor::DrawFields(const std::vector<LoomObject*>& objects, const std::vector<std::string>& labels, float column)
	{
		LoomObject& object = *objects.front();

		ImGui::PushItemWidth(-FLT_MIN);

		const std::vector<const std::vector<FieldNames::Enumerator>*> enumerators = FieldNames::EnumeratorsOf(object);

		// Deeper nested fields indent their labels but keep their values in one column.
		const float left = ImGui::GetCursorPosX();

		const auto draw_field = [&](size_t i)
		{
			const SerializedField& field = object.GetFields()[i];

			if (field.type == FieldType::Uniforms || !object.IsShown(field))
				return;

			ImGui::PushID(&field);

			Label(labels[i], (std::max)(column - (ImGui::GetCursorPosX() - left), 0.0f));

			const char* label = "##value";
			bool changed = false;

			// Vertex data is not edited here, and too long to write every frame.
			const bool editable = field.type != FieldType::FloatArray && field.type != FieldType::UIntArray;
			const std::vector<std::string> before = m_playing && editable ? Written(objects, i) : std::vector<std::string>();

			// A vector is mixed per axis, and a list is drawn as the first object has it.
			const size_t axes = MultiEdit::AxisCount(object, i);
			const MultiEdit::Axes mixed_axes = axes > 0 ? MultiEdit::MixedAxes(objects, i) : MultiEdit::Axes{ };
			const bool mixed = editable && axes == 0 && field.type != FieldType::AssetList && MultiEdit::Mixed(objects, i);

			std::array<float, MultiEdit::max_axes> was{ };
			std::copy_n((const float*)field.data, axes, was.begin());

			const char* integer_format = mixed ? MIXED_VALUE : nullptr;

			// Rounded through "-", a drag would start from 0.
			const ImGuiSliderFlags mixed_flags = mixed ? ImGuiSliderFlags_NoRoundToFormat : ImGuiSliderFlags_None;
			m_mixedField = mixed;

			const std::vector<std::string> choices = object.GetFieldChoices(field);

			if (!choices.empty())
				changed = DrawChoiceField(label, field, choices, mixed);
			else if (enumerators[i])
				changed = DrawEnumField(label, field, *enumerators[i], mixed);
			else switch (field.type)
			{
			case FieldType::Bool:
				ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
				changed = ImGui::Checkbox(label, (bool*)field.data);
				ImGui::PopItemFlag();
				break;

			case FieldType::Int:
				changed = ImGui::DragScalar(label, ImGuiDataType_S32, field.data, integer_drag_speed, nullptr, nullptr, integer_format);
				break;

			case FieldType::UInt:
				changed = ImGui::DragScalar(label, ImGuiDataType_U32, field.data, integer_drag_speed, nullptr, nullptr, integer_format);
				break;

			case FieldType::Int64:
				changed = ImGui::DragScalar(label, ImGuiDataType_S64, field.data, integer_drag_speed, nullptr, nullptr, integer_format);
				break;

			case FieldType::UInt64:
				changed = ImGui::DragScalar(label, ImGuiDataType_U64, field.data, integer_drag_speed, nullptr, nullptr, integer_format);
				break;

			case FieldType::Float:
				changed = ImGui::DragFloat(label, (float*)field.data, drag_speed, 0.0f, 0.0f, mixed ? MIXED_VALUE : float_format, mixed_flags);
				break;

			case FieldType::Double:
				changed = ImGui::DragScalar(label, ImGuiDataType_Double, field.data, drag_speed, nullptr, nullptr, mixed ? MIXED_VALUE : double_format, mixed_flags);
				break;

			case FieldType::String:
			{
				std::string& text = *(std::string*)field.data;

				if (text.find('\n') != std::string::npos)
				{
					ImGui::Text("%d lines", (int)std::count(text.begin(), text.end(), '\n'));
					break;
				};

				if (mixed)
				{
					std::string typed{ };

					if (InputTextString(label, typed, MIXED_VALUE))
					{
						text = typed;
						changed = true;
					};
				}
				else changed = InputTextString(label, text);

				changed |= AcceptAssetDrop(text);
				changed |= AcceptScriptsDrop(text, ScriptEntryKind::Shader);
			}
			break;

			case FieldType::MultilineText:
			{
				std::string& text = *(std::string*)field.data;
				const int lines = std::clamp((int)std::count(text.begin(), text.end(), '\n') + 2, min_text_lines, max_text_lines);
				const float height = ImGui::GetTextLineHeight() * lines + ImGui::GetStyle().FramePadding.y * 2.0f;

				// A multiline box has no hint, so a mixed one starts empty.
				if (mixed)
				{
					std::string typed{ };

					if (InputTextMultilineString(label, typed, height))
					{
						text = typed;
						changed = true;
					};
				}
				else changed = InputTextMultilineString(label, text, height);
			}
			break;

			case FieldType::Vec2:
			case FieldType::Vec3:
			case FieldType::Vec4:
				changed = AxisGui::DragFloatN(label, (float*)field.data, (int)axes, mixed_axes.data(), drag_speed, float_format);
				break;

			case FieldType::FloatArray:
			{
				// Vertex data: worth seeing the size of, not worth 3000 drag boxes.
				const std::vector<float>& values = *(std::vector<float>*)field.data;

				ImGui::Text("%d floats", (int)values.size());
			}
			break;

			case FieldType::UIntArray:
				ImGui::Text("%d words", (int)((std::vector<unsigned int>*)field.data)->size());
				break;

			case FieldType::Reference:
				changed = DrawReferenceField(object, field);
				break;

			case FieldType::Asset:
				changed = DrawAssetField(field);
				break;

			case FieldType::AssetList:
				changed = DrawAssetListField(field);
				break;

			case FieldType::State:
				changed = StateMachineMonitor::DrawStateField(*(StateReference*)field.data);
				break;

			case FieldType::Uniforms:
				break;
			};

			m_mixedField = false;

			if (changed)
			{
				field.Changed();
				object.OnFieldChanged(field);
				MultiEdit::Spread(objects, i, axes > 0 ? was.data() : nullptr);

				if (m_playing)
					TrackPlayEdit(objects, i, before);
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

			if (ImGui::Button(std::string(NameDisplay(button.name)).c_str(), ImVec2(-FLT_MIN, 0.0f)))
				for (LoomObject* each : objects)
					if (const LoomObject::FieldButton& pressed = each->GetButtons()[item.index]; pressed.press && *pressed.press)
						(*pressed.press)();

			ImGui::PopID();
		};

		DrawFieldGroup(GroupFields(object), draw_item);

		ImGui::PopItemWidth();
	};

	bool Editor::DrawReferenceField(const LoomObject& object, const SerializedField& field)
	{
		LoomObject* target = field.GetReference();

		DrawPickerButton(target ? Describe(*target) : "None");

		bool changed = false;

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

		if (!BeginPicker())
			return changed;

		const GameObject* holder = dynamic_cast<const GameObject*>(&object);

		if (const ComponentBase* component = dynamic_cast<const ComponentBase*>(&object))
			holder = component->GetGameObject();

		GameObject* prefab = GetPrefabRoot();
		std::vector<LoomObject*> tops{ };

		if (holder && prefab && Hierarchy::Contains(*prefab, holder))
			tops.push_back(prefab);
		else if (Scene* scene = holder ? EditCommands::SceneOf(*holder) : nullptr)
			tops.push_back(scene);
		else
			tops.assign(Scene::GetScenes().begin(), Scene::GetScenes().end());

		VirtualFolders::Node listed{ };
		std::unordered_map<std::string, LoomObject*> candidates{ };

		for (LoomObject* top : tops)
			for (LoomObject* candidate : field.ReferencesUnder(*top))
			{
				const std::string key = candidate->GetGuid().ToString();

				listed.items.push_back(key);
				candidates.emplace(key, candidate);
			};

		if (PickerRow("", "None", target == nullptr) && target != nullptr)
		{
			field.SetReference(nullptr);
			changed = true;
		};

		VirtualFolders flat{ };

		FolderTree::Hooks hooks{ };
		hooks.arrange = false;
		hooks.name = [&](const std::string& key) { return Describe(*candidates.at(key)); };
		hooks.item = [&](const std::string& key)
		{
			LoomObject* candidate = candidates.at(key);

			if (PickerRow(key, Describe(*candidate), candidate == target) && candidate != target)
			{
				field.SetReference(candidate);
				changed = true;
			};
		};

		m_fieldPicker.Draw(flat, listed, hooks);

		EndPicker();

		return changed;
	};

	#pragma endregion

	#pragma endregion

	#pragma region Views

	void Editor::DrawPrefab()
	{
		DrawPrefabQuestion();

		// Closed with edits held: asked first, and closed once answered.
		if (!m_showPrefab && m_prefabUnsaved)
		{
			m_showPrefab = true;
			m_prefabNext = std::string();
		};

		if (!m_showPrefab || (m_prefabPath.empty() && m_prefabBeforeBuild.empty()))
		{
			m_showPrefab = false;
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

		m_prefabFocused = false;

		if (ImGui::Begin(title.c_str(), &m_showPrefab, view_flags | (m_prefabUnsaved ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None)))
		{
			m_prefabFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

			Watch(View::Prefab);

			DrawPrefabView();
		};

		ImGui::End();
	};

	void Editor::DrawPrefabView()
	{
		LOOM_STAGE("Editor::DrawPrefabView");

		if (GetPrefabRoot() != nullptr)
			DrawView(m_prefabView, { m_prefabScene }, &m_prefabCamera, &m_prefabOutline);
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
		LOOM_STAGE("Editor::DrawGameView");

		m_gamePanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		m_gamePanelHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

		if (Scene::GetScenes().empty())
			DrawNoScene();
		else
		{
			// GLFW reports from the main window's corner, which only matches ImGui without viewports.
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
		LOOM_STAGE("Editor::DrawDebugView");

		if (Scene::GetScenes().empty())
			DrawNoScene();
		else
		{
			DrawView(m_debugView, Scene::GetScenes(), &m_debugCamera, &m_debugOutline);

			if (m_activeScene)
				TakeImportTarget(&m_activeScene->GetRoot());
		};
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

		// A tab is drawn in its dock node's host window, and a hidden tab bar keeps its last rect.
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

	void Editor::DrawToolbar()
	{
		constexpr const char* labels[] =
		{
			EditorIcons::Play, EditorIcons::Pause, EditorIcons::Stop, EditorIcons::Step, EditorIcons::Build,
		};
		constexpr float frame_sides = 2.0f;

		float label_width = 0.0f;

		for (const char* label : labels)
			label_width = (std::max)(label_width, ImGui::CalcTextSize(label).x);

		const ImVec2 button_size(label_width + ImGui::GetStyle().FramePadding.x * frame_sides, 0.0f);

		const auto button = [button_size](const char* icon, const char* tooltip, bool enabled)
		{
			ImGui::BeginDisabled(!enabled);

			const bool pressed = ImGui::Button(icon, button_size);

			ImGui::SetItemTooltip("%s", tooltip);
			ImGui::EndDisabled();
			ImGui::SameLine();

			return pressed;
		};

		if (button(EditorIcons::Play, "Play", !Engine::updateScenes && !m_playAfterCompile))
			TogglePlay();

		if (button(EditorIcons::Pause, "Pause", Engine::updateScenes))
			TogglePlay();

		if (button(EditorIcons::Stop, "Stop", IsPlaying()))
			Stop();

		if (button(EditorIcons::Step, "Step", !m_scripts.IsBuilding() && !Engine::updateScenes))
		{
			BeginPlay();
			m_steppedLastFrame = true;
		};

		if (button(EditorIcons::Build, "Compile", m_scripts.HasProject() && !m_scripts.IsBuilding() && !IsPlaying()))
			CompileScripts();

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

		// Steered before drawing, so the image keeps up with the mouse.
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

		std::vector<GameObject*> ringed{ };

		if (outline && seen_through)
			for (GameObject* gameObject : GetSelection())
				if (std::any_of(
					scenes.begin(),
					scenes.end(),
					[gameObject](const Scene* scene) { return Hierarchy::Contains(scene->GetRoot(), gameObject); }))
					ringed.push_back(gameObject);

		// ImGui only records the texture id now and draws it at submit.
		view.Render(
			(int)available.x,
			(int)available.y,
			Engine::clearColor,
			[&]()
			{
				if (!camera)
					Ui::BeginOverlay();

				for (Scene* scene : scenes)
					scene->Render(seen_through, scenes);
			},
			[&]()
			{
				if (!camera)
					Ui::DrawOverlay();

				if (!ringed.empty())
					outline->Draw(ringed, *seen_through);

				for (GameObject* gameObject : ringed)
					m_colliderGizmos.Draw(*gameObject, *seen_through);
			});

		if (view.GetTexture())
			ImGui::Image(
				view.GetTextureID(),
				ImVec2((float)view.GetWidth(), (float)view.GetHeight()),
				ImVec2(0.0f, 1.0f),	// The framebuffer is bottom-up,
				ImVec2(1.0f, 0.0f));// so the V axis is flipped
	};

	void Editor::DrawSceneData()
	{
		if (ImGui::Begin(EditorPanels::SceneData, &m_showSceneData))
		{
			if (m_activeScene == nullptr)
				ImGui::TextDisabled("No scene open.");
			else
			{
				// Rebuilt every frame, so it is the scene as it would be written now.
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

#ifndef LOOM_FINAL
			if (ImGui::CollapsingHeader("Frame stages"))
			{
				m_stagesShown = true;
				StageTiming::Details();
			};
#endif
		};

		ImGui::End();
	};

	#pragma endregion

	#pragma region Queued Input

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

		if (!m_scripts.IsIdle())
		{
			m_importTarget = nullptr;
			m_importFolder.clear();
			m_importPlaced = false;
			return;
		};

		const QueuedImport& front = m_importQueue.front();

		// The mouse event lands next frame, and the panels drawn then pick the target.
		if (front.positioned && !m_importPlaced)
		{
			ImGui::GetIO().AddMousePosEvent(front.x, front.y);
			m_importPlaced = true;
			m_importTarget = nullptr;
			m_importFolder.clear();
			return;
		};

		// Files dropped together go in together; others wait for the mouse.
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

	void Editor::QueueTestRun(const std::string& filter)
	{
		m_tests.SetFilter(filter);
		m_tests.QueueShown();
	};

	void Editor::RunQueuedTests()
	{
		if (!m_tests.HasQueued() || !m_scripts.IsIdle())
			return;

		if (m_scripts.HasProject() && m_scripts.IsStale() && !std::exchange(m_testsCompiled, true))
		{
			CompileScripts();
			return;
		};

		m_testsCompiled = false;
		m_tests.RunQueued();
		m_showTests = true;
		m_focusTests = !m_tests.HasQueued();
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
			std::cerr << "Project: " << file << " is not a file any of the Project panel's tabs lists" << std::endl;
			return;
		};

		std::string error;
		const std::string copied = ProjectAssets::CopyInto(file, m_importFolder, &error);

		if (copied.empty())
		{
			std::cerr << "Project: " << error << std::endl;
			return;
		};

		if (RevealFile(copied))
			return;

		m_selectedAsset = std::filesystem::path(copied).lexically_normal().string();
		m_revealSelectedAsset = true;
	};

	#pragma endregion

};
