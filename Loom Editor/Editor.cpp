#include "Editor.h"

#include "EditorLog.h"
#include "EditorTheme.h"

#include "Component.h"
#include "ComponentRegistry.h"
#include "EditorGui.h"
#include "Engine.h"
#include "GameObject.h"
#include "LoomObject.h"
#include "Material.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "Transform.h"

#include "Utilities/AxisGui.h"
#include "Utilities/StateMachine.h"
#include "Utilities/StateMachineMonitor.h"

#include "EditCommands.h"
#include "EditorFileDialogs.h"
#include "FieldNames.h"
#include "ModelImporter.h"
#include "ProjectAssets.h"
#include "ProjectTemplate.h"

#include "OpenGL.h"

#include "glm/gtc/type_ptr.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

		GameObject* FindByName(GameObject& root, const std::string& name)
		{
			if (root.GetName() == name)
				return &root;

			for (GameObject* child : root.GetChildren())
				if (GameObject* found = FindByName(*child, name))
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

		// How often the editor checks whether a shader file was edited outside it.
		constexpr double shader_poll_seconds = 0.5;

		const GameObject* RootOf(const GameObject* gameObject)
		{
			while (gameObject->GetParent() != nullptr)
				gameObject = gameObject->GetParent();

			return gameObject;
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

		constexpr const char* ellipsis = "...";

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

		// Hierarchy nodes hand over their object's guid rather than its address,
		// so a drop resolves against what still exists when the mouse comes up.
		constexpr const char* object_payload = "LoomObject";

		void DragSource(const LoomObject& object)
		{
			if (!ImGui::BeginDragDropSource())
				return;

			ImGui::SetDragDropPayload(object_payload, &object.GetGuid(), sizeof(Guid));
			ImGui::TextUnformatted(object.NameAndID().c_str());

			ImGui::EndDragDropSource();
		};

		// The object being dragged, as the field would hold it. Null when nothing
		// is being dragged or the field has no place for it, which also keeps the
		// field from lighting up under a drag it would refuse.
		LoomObject* DraggedInto(const SerializedField& field)
		{
			const ImGuiPayload* payload = ImGui::GetDragDropPayload();

			if (payload == nullptr || !payload->IsDataType(object_payload))
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

			if (target && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", target->GetGuid().ToString().c_str());

			if (LoomObject* dropped = DraggedInto(field))
				if (ImGui::BeginDragDropTarget())
				{
					if (ImGui::AcceptDragDropPayload(object_payload))
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

		// The engine keeps every floating window in an OS window of its own,
		// which for an editor means dialogs that sit behind the main window and
		// take clicks with them. Floating windows belong inside the editor
		// until the user drags one out.
		ImGui::GetIO().ConfigViewportsNoAutoMerge = false;

		// Clicking a number field types into it, Unity-style; dragging still scrubs.
		ImGui::GetIO().ConfigDragClickToInputText = true;

		ImGui::GetIO().PlatformOpenInShellFn = OpenInShell;

		drop_receiver = this;

		LoadSettings();
		m_settings.Apply();
	};

	Editor::~Editor()
	{
		SetDropCallback(nullptr);
		drop_receiver = nullptr;

		Engine::SetGuiFunction(Task{ });

		Engine::renderScenes = true;
		Engine::updateScenes = true;

		for (Scene* scene : m_ownedScenes)
			delete scene;

		m_ownedScenes.clear();

		EditorLog::Get().Uninstall();

		if (m_assetWatch != nullptr)
			FindCloseChangeNotification(m_assetWatch);
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

			if (line.rfind("opened=", 0) == 0)
				m_lastOpenedScene = line.substr(strlen("opened="));

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

		if (!m_lastOpenedScene.empty())
			out << "opened=" << m_lastOpenedScene << '\n';
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

		// A project file stands for the folder it is in.
		if (std::filesystem::path(path).extension() == ProjectTemplate::extension &&
			std::filesystem::is_regular_file(path, code))
			return OpenProject(std::filesystem::absolute(path, code).parent_path().string(), open_scene);

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

		CloseAllScenes();

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

	std::string Editor::DefaultPathFor(Scene* scene) const
	{
		const std::string name =
			(scene ? scene->GetName() : std::string("Untitled")) + SceneSerializer::extension;

		return m_projectPath.empty()
			? "Scenes/" + name
			: (std::filesystem::path(m_projectPath) / name).string();
	};

	bool Editor::IsOwned(Scene* scene) const
	{
		return std::find(
			m_ownedScenes.begin(),
			m_ownedScenes.end(),
			scene) != m_ownedScenes.end();
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

		// Deleting a scene mid-GUI would leave the rest of the frame walking a
		// dangling pointer; deferring puts the delete (and the unregistering it
		// queues in turn) at the top of the next frame instead.
		m_scenePaths.erase(scene);
		m_history.Forget(scene->GetGuid());

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
		if (CanStepHistory())
			m_history.Undo(m_ownedScenes, [this](Scene* scene, const std::string& text) { return ReplaceScene(scene, text); });
	};

	void Editor::Redo()
	{
		if (CanStepHistory())
			m_history.Redo(m_ownedScenes, [this](Scene* scene, const std::string& text) { return ReplaceScene(scene, text); });
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

		if (!m_playing)
			m_history.Record(m_ownedScenes);
	};

	Scene* Editor::ReplaceScene(Scene* scene, const std::string& text)
	{
		const auto slot = std::find(m_ownedScenes.begin(), m_ownedScenes.end(), scene);
		const Guid selected = m_selected ? m_selected->GetGuid() : Guid();
		const bool active = scene == m_activeScene;
		const std::string path = m_scenePaths[scene];

		m_scenePaths.erase(scene);

		if (active)
			m_activeScene = nullptr;

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

		Select(LoomObject::GetByGuid<GameObject>(selected));

		return replacement;
	};

	void Editor::HandleShortcuts()
	{
		switch (EditCommands::FromShortcut(m_showHierarchy && m_hierarchyFocused))
		{
		case EditCommand::Undo:		Undo();				break;
		case EditCommand::Redo:		Redo();				break;
		case EditCommand::Cut:		Cut();				break;
		case EditCommand::Copy:		Copy();				break;
		case EditCommand::Paste:	Paste();			break;
		case EditCommand::Delete:	DeleteSelected();	break;
		case EditCommand::None:							break;
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

		// Panels come and go as OS windows, so the callback is put on whichever
		// exist this frame.
		SetDropCallback(OnDrop);

		DrawMainMenuBar();

		const ImGuiID dockspace_id =
			ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

		// Only lay the panels out when there is nothing to restore: from the
		// second run on, imgui.ini holds wherever the user dragged them.
		if (m_firstFrame)
		{
			const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace_id);

			if (node == nullptr || node->IsEmpty())
				m_rebuildLayout = true;

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
		};

		if (m_showProject)		DrawProject();
		if (m_showHierarchy)	DrawHierarchy();
		if (m_showInspector)	DrawInspector();
		if (m_showScene)		DrawScene();
		if (m_showSceneData)	DrawSceneData();
		if (m_showConsole)		DrawConsole();
		if (m_showStats)		DrawStats();
		if (m_showImGuiDemo)	ImGui::ShowDemoWindow(&m_showImGuiDemo);

		if (m_showSettings && m_settings.Draw(&m_showSettings))
			SaveSettings();

		ImportQueued();
		PollScripts();

		// The renderer reads the window back once this frame has finished
		// drawing, so the bitmap is the whole editor.
		if (!m_screenshotPath.empty() && ++m_frame >= m_screenshotFrame)
			CaptureFrame();

		DrawFolderPrompt();
		DrawNewProjectPrompt();
		DrawCreateAssetPrompt();
		DrawFileDialogs();

		if (ImGui::IsAnyItemActive())
			m_historyDirty = true;

		for (int button = 0; button < ImGuiMouseButton_COUNT; button++)
			if (ImGui::IsMouseReleased(button))
				m_historyDirty = true;
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

		ImGui::DockBuilderDockWindow("Hierarchy", left);
		ImGui::DockBuilderDockWindow("Project", left_bottom);
		ImGui::DockBuilderDockWindow("Inspector", right);
		ImGui::DockBuilderDockWindow("Console", bottom);
		ImGui::DockBuilderDockWindow("Scene Data", bottom);
		ImGui::DockBuilderDockWindow("Stats", bottom);

		// Whatever is left in the middle is the selected scene.
		ImGui::DockBuilderDockWindow("Scene", centre);

		ImGui::DockBuilderFinish(dockspace_id);
	};

	void Editor::DrawMainMenuBar()
	{
		if (!ImGui::BeginMainMenuBar())
			return;

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Project..."))
			{
				snprintf(
					m_newProjectFolder,
					sizeof(m_newProjectFolder),
					"%s",
					DesktopPath().c_str());

				m_askForNewProject = true;
			};

			if (ImGui::MenuItem("New Scene"))
				CreateScene("Scene " + std::to_string(++m_sceneCounter));

			if (ImGui::MenuItem("Open Scene..."))
				ShowOpenSceneDialog();

			if (ImGui::MenuItem("Save Scene As...", nullptr, false, m_activeScene != nullptr))
			{
				const std::filesystem::path path(DefaultPathFor(m_activeScene));

				if (path.has_parent_path())
				{
					std::error_code code;
					std::filesystem::create_directories(path.parent_path(), code);
					m_dialogs->saveScene.SetCurrentDirectory(path.parent_path());
				};

				m_dialogs->saveScene.SetInputName(path.filename().string());
				m_dialogs->saveScene.Open();
			};

			if (ImGui::MenuItem("Close Scene", nullptr, false, IsOwned(m_activeScene)))
				CloseScene(m_activeScene);

			ImGui::Separator();

			if (ImGui::MenuItem("Open Scene Folder..."))
				m_dialogs->folder.Open();

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
			ImGui::MenuItem("Hierarchy", nullptr, &m_showHierarchy);
			ImGui::MenuItem("Project", nullptr, &m_showProject);
			ImGui::MenuItem("Inspector", nullptr, &m_showInspector);
			ImGui::MenuItem("Scene", nullptr, &m_showScene);
			ImGui::MenuItem("Scene Data", nullptr, &m_showSceneData);
			ImGui::MenuItem("Console", nullptr, &m_showConsole);
			ImGui::MenuItem("Stats", nullptr, &m_showStats);

			ImGui::Separator();

			if (ImGui::MenuItem("Reset Layout"))
				m_rebuildLayout = true;

			ImGui::MenuItem("ImGui Demo", nullptr, &m_showImGuiDemo);

			ImGui::Separator();
			ImGui::MenuItem("Settings", nullptr, &m_showSettings);

			ImGui::EndMenu();
		};

		// Play control, mirrored in the Scene window's toolbar.
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
			ImGui::TextDisabled("Everything under it shows up in the Project panel.");

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

	void Editor::ShowOpenSceneDialog()
	{
		const std::filesystem::path last(m_lastOpenedScene);
		std::error_code code;

		if (!m_lastOpenedScene.empty() &&
			std::filesystem::is_directory(last.parent_path(), code))
			m_dialogs->openScene.SetCurrentDirectory(last.parent_path());
		else if (!m_projectPath.empty())
			m_dialogs->openScene.SetCurrentDirectory(m_projectPath);

		m_dialogs->openScene.Open();
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

		m_dialogs->openScene.Display();

		if (m_dialogs->openScene.HasSelected())
		{
			const std::string path = m_dialogs->openScene.GetSelected().string();

			if (OpenScene(path))
			{
				m_lastOpenedScene = path;
				SaveSettings();
			};

			m_dialogs->openScene.ClearSelected();
		};

		m_dialogs->saveScene.Display();

		if (m_dialogs->saveScene.HasSelected())
		{
			std::filesystem::path path = m_dialogs->saveScene.GetSelected();

			// Typing a name without the extension is the common case.
			if (path.extension() != SceneSerializer::extension)
				path += SceneSerializer::extension;

			SaveScene(m_activeScene, path.string());

			m_dialogs->saveScene.ClearSelected();

			RefreshProjectAssets();
		};
	};

	void Editor::DrawHierarchy()
	{
		m_hierarchyFocused = false;

		if (ImGui::Begin("Hierarchy", &m_showHierarchy))
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
		};

		ImGui::End();
	};

	void Editor::DrawSceneNode(Scene* scene)
	{
		GameObject& root = scene->GetRoot();

		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_DefaultOpen;

		if (m_selected == &root)
			flags |= ImGuiTreeNodeFlags_Selected;

		const bool is_active = scene == m_activeScene;

		if (is_active)
			ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::AccentText);

		if (m_expand.contains(root.GetGuid()))
			ImGui::SetNextItemOpen(true);

		const bool open = ImGui::TreeNodeEx(
			scene->GetGuid().ToString().c_str(),
			flags,
			"%s%s",
			scene->NameAndID().c_str(),
			is_active ? "  [active]" : "");

		if (is_active)
			ImGui::PopStyleColor();

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", scene->GetGuid().ToString().c_str());

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
				SetActiveScene(scene);

			if (ImGui::MenuItem("Add GameObject"))
				Select(scene->AddChild());

			ImGui::Separator();

			if (ImGui::MenuItem("Close Scene", nullptr, false, IsOwned(scene)))
				CloseScene(scene);

			ImGui::EndPopup();
		};

		if (!open)
			return;

		const std::vector<GameObject*> children = root.GetChildren();

		for (GameObject* child : children)
			DrawGameObjectNode(child);

		ImGui::TreePop();
	};

	void Editor::DrawGameObjectNode(GameObject* gameObject)
	{
		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth;

		if (gameObject->GetChildren().empty())
			flags |= ImGuiTreeNodeFlags_Leaf;

		if (m_selected == gameObject)
			flags |= ImGuiTreeNodeFlags_Selected;

		if (m_expand.contains(gameObject->GetGuid()))
			ImGui::SetNextItemOpen(true);

		const bool renaming = m_renaming && m_selected == gameObject;

		const bool open = ImGui::TreeNodeEx(
			gameObject->GetGuid().ToString().c_str(),
			flags,
			"%s",
			renaming ? "" : gameObject->GetName().c_str());

		if (ImGui::IsItemHovered() && !renaming)
			ImGui::SetTooltip("%s", gameObject->GetGuid().ToString().c_str());

		TakeImportTarget(gameObject);
		DragAndDropNode(gameObject, open);

		DragSource(*gameObject);

		// Not again when already selected: the release that ends a double-click
		// would cancel the rename its press just started.
		if (NodeReleased() && m_selected != gameObject)
			Select(gameObject);

		if (ImGui::IsItemHovered() &&
			ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
			!ImGui::IsItemToggledOpen())
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

			if (ImGui::MenuItem("Delete", nullptr, false, EditCommands::CanTake(m_selected)))
				DeleteSelected();

			ImGui::EndPopup();
		};

		if (renaming)
			DrawRenameField(gameObject);

		if (!open)
			return;

		const std::vector<GameObject*> children = gameObject->GetChildren();

		for (GameObject* child : children)
			DrawGameObjectNode(child);

		ImGui::TreePop();
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

		if (DropShader(gameObject))
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

		if (dragged == nullptr ||
			IsInHierarchy(*dragged, gameObject) ||
			RootOf(dragged) != RootOf(gameObject))
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
			dragged->SetParent(parent, before);
			Select(dragged);

			m_historyDirty = true;
		};

		ImGui::EndDragDropTarget();
	};

	void Editor::DrawRenameField(GameObject* gameObject)
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);

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

	bool Editor::DropShader(GameObject* gameObject)
	{
		const ImGuiPayload* dragged = ImGui::GetDragDropPayload();

		if (dragged == nullptr || !dragged->IsDataType(ASSET_PAYLOAD))
			return false;

		const std::string path = (const char*)dragged->Data;

		// Anything else dragged out of the Project panel has no place here.
		if (std::filesystem::path(path).extension() != ProjectAssets::shaderExtension)
			return true;

		if (!ImGui::AcceptDragDropPayload(ASSET_PAYLOAD))
			return true;

		Material* material = gameObject->GetComponent<Material>();

		if (material == nullptr)
			material = gameObject->Attach<Material>();

		material->ChangeShader(path);

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

	void Editor::DrawInspector()
	{
		if (ImGui::Begin("Inspector", &m_showInspector))
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

				if (ImGui::BeginPopup("##add_component"))
				{
					if (ComponentRegistry::All().empty())
						ImGui::TextDisabled("Nothing registered.");

					for (const auto& [name, factory] : ComponentRegistry::All())
						if (ImGui::MenuItem(name.c_str()))
							factory(*gameObject);

					ImGui::EndPopup();
				};
			};
		};

		ImGui::End();
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

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", component->GetGuid().ToString().c_str());

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

		DrawFields(*component, labels, LabelColumn(labels, 0.0f));

		if (dynamic_cast<StateMachineBase*>(component))
		{
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float half = (ImGui::GetContentRegionAvail().x - spacing) / 2.0f;

			if (ImGui::Button("New State", ImVec2(half, 0.0f)))
				AskForAsset(AssetKind::State, m_projectPath);

			ImGui::SameLine(0.0f, spacing);

			if (ImGui::Button("New State Machine", ImVec2(half, 0.0f)))
				AskForAsset(AssetKind::StateMachine, m_projectPath);
		};

		component->OnGui();

		ImGui::PopID();
	};

	void Editor::DrawFields(LoomObject& object, const std::vector<std::string>& labels, float column)
	{
		ImGui::PushItemWidth(-FLT_MIN);

		const std::vector<const std::vector<FieldNames::Enumerator>*> enumerators = FieldNames::EnumeratorsOf(object);

		for (size_t i = 0; i < object.GetFields().size(); i++)
		{
			const SerializedField& field = object.GetFields()[i];

			if (field.type == FieldType::Uniforms)
				continue;

			ImGui::PushID(&field);

			Label(labels[i], column);

			const char* label = "##value";
			bool changed = false;

			if (enumerators[i])
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
				changed = InputTextString(label, *(std::string*)field.data);
				changed |= AcceptAssetDrop(*(std::string*)field.data);
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
				object.OnFieldChanged(field);

			ImGui::PopID();
		};

		ImGui::PopItemWidth();
	};

	void Editor::DrawScene()
	{
		if (ImGui::Begin(
			"Scene",
			&m_showScene,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			if (ImGui::Button(Engine::updateScenes ? "Pause" : "Play"))
				TogglePlay();

			ImGui::SameLine();
			ImGui::BeginDisabled(!IsPlaying());

			if (ImGui::Button("Stop"))
				Stop();

			ImGui::EndDisabled();

			ImGui::SameLine();

			// A build has the scenes down, so there is nothing to step.
			ImGui::BeginDisabled(m_scripts.IsBuilding());

			if (ImGui::Button("Step"))
			{
				BeginPlay();
				m_steppedLastFrame = true;
			};

			ImGui::EndDisabled();

			ImGui::SameLine();
			ImGui::SetNextItemWidth(220.0f);

			if (ImGui::BeginCombo(
				"##scene",
				m_activeScene
					? m_activeScene->NameAndID().c_str()
					: "No scene"))
			{
				for (Scene* scene : Scene::GetScenes())
					if (ImGui::Selectable(
						scene->NameAndID().c_str(),
						scene == m_activeScene))
						SetActiveScene(scene);

				ImGui::EndCombo();
			};

			ImGui::SameLine();
			ImGui::ColorEdit4(
				"Background",
				Engine::clearColor,
				ImGuiColorEditFlags_NoInputs);

			ImGui::Separator();

			const ImVec2 available = ImGui::GetContentRegionAvail();

			if (Scene::GetScenes().empty())
				ImGui::TextDisabled("No scene open. Pick one in the Project panel.");
			else if (available.x >= 1.0f && available.y >= 1.0f)
			{
				// Rendering here is fine mid-frame: ImGui only records the
				// texture id now and draws with it once the frame is submitted.
				m_viewport.Render((int)available.x, (int)available.y);

				if (m_viewport.GetTexture())
					ImGui::Image(
						m_viewport.GetTextureID(),
						ImVec2(
							(float)m_viewport.GetWidth(),
							(float)m_viewport.GetHeight()),
						ImVec2(0.0f, 1.0f),	// The framebuffer is bottom-up,
						ImVec2(1.0f, 0.0f));// so the V axis is flipped

				if (m_activeScene)
					TakeImportTarget(&m_activeScene->GetRoot());
			};
		};

		ImGui::End();
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
			return;
		};

		GameObject* parent = m_importTarget ? m_importTarget : m_selected;

		if (parent == nullptr)
			parent = m_activeScene
				? &m_activeScene->GetRoot()
				: &CreateScene("Scene " + std::to_string(++m_sceneCounter))->GetRoot();

		// Files dropped together go in together; a drop somewhere else waits
		// for the mouse to be put there.
		const auto together =
			[&front](const QueuedImport& queued)
			{
				return queued.positioned == front.positioned && queued.x == front.x && queued.y == front.y;
			};

		const auto end = std::find_if_not(m_importQueue.begin(), m_importQueue.end(), together);

		for (auto queued = m_importQueue.begin(); queued != end; ++queued)
			if (!IsModelFile(queued->path))
				std::cerr << "Import: " << queued->path << " is not a model format Assimp reads" << std::endl;
			else if (GameObject* model = ImportModel(queued->path, *parent))
			{
				Select(model);
				m_historyDirty = true;
			};

		m_importQueue.erase(m_importQueue.begin(), end);
		m_importTarget = nullptr;
		m_importPlaced = false;
	};

	void Editor::DrawSceneData()
	{
		if (ImGui::Begin("Scene Data", &m_showSceneData))
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

	void Editor::DrawConsole()
	{
		if (ImGui::Begin("Console", &m_showConsole))
		{
			if (ImGui::Button("Clear"))
				EditorLog::Get().Clear();

			ImGui::SameLine();
			ImGui::Checkbox("Auto-scroll", &m_consoleAutoScroll);

			ImGui::SameLine();
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint(
				"##filter",
				"Filter",
				m_consoleFilter,
				sizeof(m_consoleFilter));

			ImGui::Separator();

			if (ImGui::BeginChild(
				"##lines",
				ImVec2(0.0f, 0.0f),
				ImGuiChildFlags_None,
				ImGuiWindowFlags_HorizontalScrollbar))
			{
				const char* filter = m_consoleFilter;

				EditorLog::Get().ForEach(
					[filter](const std::string& line, bool is_error)
					{
						if (filter[0] && line.find(filter) == std::string::npos)
							return;

						if (is_error)
							ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::ErrorText);

						ImGui::TextUnformatted(line.c_str());

						if (is_error)
							ImGui::PopStyleColor();
					});

				if (m_consoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
					ImGui::SetScrollHereY(1.0f);
			};

			ImGui::EndChild();
		};

		ImGui::End();
	};

	void Editor::DrawStats()
	{
		if (ImGui::Begin("Stats", &m_showStats))
		{
			const ImGuiIO& io = ImGui::GetIO();

			constexpr float milliseconds_per_second = 1000.0f;

			// ImGui already averages Framerate over its last 60 frames.
			ImGui::Text(
				"%d FPS (%d ms/frame)",
				(int)std::ceil(io.Framerate),
				(int)std::ceil(milliseconds_per_second / io.Framerate));
			ImGui::Text("Graphics API: %s", EditorSettings::BackendLabel(Engine::backend));

			ImGui::Text("Scenes: %d", (int)Scene::GetScenes().size());
			ImGui::Text("GameObjects: %d", (int)GameObject::GetObjectCount());
			ImGui::Text("Registered components: %d", (int)ComponentRegistry::All().size());

			const char* const updating =
				Engine::updateScenes
					? "yes"
					: "paused";

			ImGui::Text("Updating: %s", IsPlaying() ? updating : "stopped");
			ImGui::Text("Viewport: %d x %d", m_viewport.GetWidth(), m_viewport.GetHeight());

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
