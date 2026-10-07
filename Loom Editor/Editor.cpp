#include "Editor.h"

#include "EditorLog.h"

#include "Component.h"
#include "ComponentRegistry.h"
#include "Engine.h"
#include "GameObject.h"
#include "LoomObject.h"
#include "Scene.h"
#include "SceneSerializer.h"

#include "EditorFileDialogs.h"

#include "OpenGL.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


namespace Loom
{
	namespace
	{
		// Writes a 24 bit bottom-up bitmap, which is the row order GL hands back
		// and the one format worth writing without an image library.
		bool WriteBitmap(const std::string& path, int width, int height, const std::vector<uint8_t>& bgr)
		{
			std::ofstream out(path, std::ios::binary);

			if (!out)
				return false;

			const uint32_t row = ((uint32_t)width * 3 + 3) & ~3u;
			const uint32_t image = row * (uint32_t)height;
			const uint32_t offset = 14 + 40;

			const auto put16 = [&out](uint16_t v) { out.write((const char*)&v, 2); };
			const auto put32 = [&out](uint32_t v) { out.write((const char*)&v, 4); };

			out.write("BM", 2);
			put32(offset + image); put32(0); put32(offset);
			put32(40); put32((uint32_t)width); put32((uint32_t)height);
			put16(1); put16(24); put32(0); put32(image);
			put32(2835); put32(2835); put32(0); put32(0);

			const std::vector<char> padding(row - (size_t)width * 3, 0);

			for (int y = 0; y < height; y++)
			{
				out.write((const char*)bgr.data() + (size_t)y * width * 3, (std::streamsize)width * 3);
				out.write(padding.data(), (std::streamsize)padding.size());
			};

			return true;
		};

		// Sits beside the executable and holds the one thing worth remembering
		// between runs: which folder the project is in. Beside the executable
		// rather than in the working directory, because opening a project moves
		// the working directory to it.
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

		// ImGui has no std::string overload without the stdlib helper, so this
		// is the same resize callback that one uses.
		bool InputTextString(const char* label, std::string& value)
		{
			const auto resize =
				[](ImGuiInputTextCallbackData* data) -> int
				{
					if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
						return 0;

					std::string* text = (std::string*)data->UserData;
					text->resize(data->BufTextLen);
					data->Buf = text->data();

					return 0;
				};

			return ImGui::InputText(
				label,
				value.data(),
				value.capacity() + 1,
				ImGuiInputTextFlags_CallbackResize,
				resize,
				&value);
		};

		// Hierarchy nodes hand over their object's guid rather than its address,
		// so a drop resolves against what still exists when the mouse comes up.
		constexpr const char* objectPayload = "LoomObject";

		void DragSource(const LoomObject& object)
		{
			if (!ImGui::BeginDragDropSource())
				return;

			ImGui::SetDragDropPayload(objectPayload, &object.GetGuid(), sizeof(Guid));
			ImGui::TextUnformatted(object.NameAndID().c_str());

			ImGui::EndDragDropSource();
		};

		// The object being dragged, as the field would hold it. Null when nothing
		// is being dragged or the field has no place for it, which also keeps the
		// field from lighting up under a drag it would refuse.
		LoomObject* DraggedInto(const SerializedField& field)
		{
			const ImGuiPayload* payload = ImGui::GetDragDropPayload();

			if (payload == nullptr || !payload->IsDataType(objectPayload))
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

		void DrawReferenceField(const char* label, const SerializedField& field)
		{
			LoomObject* target = field.GetReference();

			const std::string shown = (target ? Describe(*target) : "none") + "###target";

			const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
			const float clear_width = ImGui::GetFrameHeight();

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
					if (ImGui::AcceptDragDropPayload(objectPayload))
						field.SetReference(dropped);

					ImGui::EndDragDropTarget();
				};

			if (target)
			{
				ImGui::SameLine(0.0f, spacing);

				if (ImGui::Button("x", ImVec2(clear_width, 0.0f)))
					field.SetReference(nullptr);
			};

			ImGui::SameLine(0.0f, spacing);
			ImGui::TextUnformatted(label);
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

		LoadSettings();
	};

	Editor::~Editor()
	{
		Engine::SetGuiFunction(Task{ });

		Engine::renderScenes = true;
		Engine::updateScenes = true;

		for (Scene* scene : m_ownedScenes)
			delete scene;

		m_ownedScenes.clear();

		EditorLog::Get().Uninstall();
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

		// So the editor comes back up on the scene that was open, rather than on
		// whichever of the project's scenes sorts first. Written from what the
		// session last made active rather than from what is open right now: a
		// rebuild saves with every scene closed, and that is not the user
		// leaving the scene behind.
		if (!m_startupScene.empty())
			out << "scene=" << m_startupScene << '\n';
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
			RefreshProjectScenes();

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

		RefreshProjectScenes();
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

	void Editor::RefreshProjectScenes()
	{
		m_projectScenes.clear();

		if (m_projectPath.empty())
			return;

		std::error_code code;

		for (const auto& entry :
			std::filesystem::recursive_directory_iterator(m_projectPath, code))
		{
			if (!entry.is_regular_file(code))
				continue;

			if (entry.path().extension() == SceneSerializer::extension)
				m_projectScenes.push_back(entry.path().lexically_normal().string());
		};

		std::sort(m_projectScenes.begin(), m_projectScenes.end());
	};

	Scene* Editor::CreateScene(const std::string& name)
	{
		Scene* scene = new Scene(name);

		m_ownedScenes.push_back(scene);
		m_scenePaths[scene] = "";

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
		m_scenePaths[scene] = std::filesystem::path(path).lexically_normal().string();

		SetActiveScene(scene);
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
		m_renaming = false;
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

	void Editor::ValidateSelection()
	{
		const std::vector<Scene*>& scenes = Scene::GetScenes();

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
					std::filesystem::current_path().string().c_str());

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

		PollScripts();

		// Queued onto the foreground draw list, which is drawn after every
		// window: by the time the callback runs, the frame in the back buffer is
		// the finished editor.
		if (!m_screenshotPath.empty() && ++m_frame >= m_screenshotFrame)
			ImGui::GetForegroundDrawList()->AddCallback(
				[](const ImDrawList*, const ImDrawCmd* command)
				{
					((Editor*)command->UserCallbackData)->CaptureFrame();
				},
				this);

		DrawFolderPrompt();
		DrawNewProjectPrompt();
		DrawFileDialogs();
	};

	void Editor::CaptureFrame()
	{
		int width = 0;
		int height = 0;

		glfwGetFramebufferSize(Engine::window, &width, &height);

		if (width > 0 && height > 0)
		{
			std::vector<uint8_t> pixels((size_t)width * height * 3);

			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glReadBuffer(GL_BACK);
			glReadPixels(0, 0, width, height, GL_BGR, GL_UNSIGNED_BYTE, pixels.data());

			std::cout
				<< (WriteBitmap(m_screenshotPath, width, height, pixels) ? "Wrote " : "Could not write ")
				<< m_screenshotPath << " (" << width << 'x' << height << ')' << std::endl;
		}
		else std::cerr << "Nothing to capture: the window has no size" << std::endl;

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
					m_projectPath.empty()
						? std::filesystem::current_path().string().c_str()
						: std::filesystem::path(m_projectPath).parent_path().string().c_str());

				m_askForNewProject = true;
			};

			if (ImGui::MenuItem("New Scene"))
				CreateScene("Scene " + std::to_string(++m_sceneCounter));

			if (ImGui::MenuItem("Open Scene..."))
			{
				if (!m_projectPath.empty())
					m_dialogs->openScene.SetCurrentDirectory(m_projectPath);

				m_dialogs->openScene.Open();
			};

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

			ImGui::Separator();

			if (ImGui::MenuItem("Exit"))
				Engine::Stop();

			ImGui::EndMenu();
		};

		if (ImGui::BeginMenu("GameObject"))
		{
			if (ImGui::MenuItem("Create Empty", nullptr, false, m_activeScene != nullptr))
				Select(m_activeScene->AddChild());

			if (ImGui::MenuItem("Create Child", nullptr, false, m_selected != nullptr))
				Select(m_selected->AddChild());

			ImGui::Separator();

			if (ImGui::MenuItem(
				"Delete",
				nullptr,
				false,
				m_selected != nullptr && m_selected->GetParent() != nullptr))
			{
				m_selected->Destroy();
				Select(nullptr);
			};

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

			ImGui::EndMenu();
		};

		// Play control, mirrored in the Scene window's toolbar.
		ImGui::Separator();

		if (ImGui::MenuItem(Engine::updateScenes ? "Pause" : "Play"))
			TogglePlay();

		if (ImGui::MenuItem("Stop", nullptr, false, IsPlaying()))
			Stop();

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
			ImGui::TextDisabled("Every %s under it shows up in the Project panel.", SceneSerializer::extension);

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
			OpenScene(m_dialogs->openScene.GetSelected().string());
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

			RefreshProjectScenes();
		};
	};

	void Editor::DrawProject()
	{
		if (ImGui::Begin("Project", &m_showProject))
		{
			if (m_projectPath.empty())
			{
				ImGui::TextDisabled("No scene folder chosen.");

				if (ImGui::Button("Choose Folder..."))
					m_dialogs->folder.Open();
			}
			else
			{
				ImGui::TextWrapped("%s", m_projectPath.c_str());

				if (ImGui::Button("Change..."))
					m_dialogs->folder.Open();

				ImGui::SameLine();

				if (ImGui::Button("Refresh"))
					RefreshProjectScenes();

				ImGui::Separator();

				if (m_projectScenes.empty())
					ImGui::TextDisabled("No %s files here.", SceneSerializer::extension);

				for (const std::string& path : m_projectScenes)
				{
					const std::string name =
						std::filesystem::path(path).filename().string();

					if (ImGui::Selectable(name.c_str()))
						OpenScene(path);

					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", path.c_str());
				};
			};

			DrawScriptsSection();
		};

		ImGui::End();
	};

	void Editor::DrawHierarchy()
	{
		if (ImGui::Begin("Hierarchy", &m_showHierarchy))
		{
			if (ImGui::Button("New Scene"))
				CreateScene("Scene " + std::to_string(++m_sceneCounter));

			ImGui::SameLine();
			ImGui::TextDisabled("%d scene(s)", (int)Scene::GetScenes().size());

			ImGui::Separator();

			// Iterating a copy: closing a scene from a context menu edits the
			// list the engine hands out.
			const std::vector<Scene*> scenes = Scene::GetScenes();

			for (Scene* scene : scenes)
				DrawSceneNode(scene);
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
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f, 0.45f, 0.85f, 1.0f));

		const bool open = ImGui::TreeNodeEx(
			(void*)scene,
			flags,
			"%s%s",
			scene->NameAndID().c_str(),
			is_active ? "  [active]" : "");

		if (is_active)
			ImGui::PopStyleColor();

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", scene->GetGuid().ToString().c_str());

		DragSource(*scene);

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

		const bool renaming = m_renaming && m_selected == gameObject;

		const bool open = ImGui::TreeNodeEx(
			(void*)gameObject,
			flags,
			"%s",
			renaming ? "" : gameObject->GetName().c_str());

		if (ImGui::IsItemHovered() && !renaming)
			ImGui::SetTooltip("%s", gameObject->GetGuid().ToString().c_str());

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

			if (ImGui::MenuItem("Delete", nullptr, false, gameObject->GetParent() != nullptr))
			{
				gameObject->Destroy();
				Select(nullptr);
			};

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

	void Editor::DrawInspector()
	{
		if (ImGui::Begin("Inspector", &m_showInspector))
		{
			if (m_selected == nullptr)
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

				ImGui::PushItemWidth(-FLT_MIN);

				ImGui::Text("Name");
				if (ImGui::InputText(
					"##name",
					m_nameBuffer,
					sizeof(m_nameBuffer),
					ImGuiInputTextFlags_EnterReturnsTrue))
					gameObject->SetName(m_nameBuffer);

				ImGui::PopItemWidth();

				DrawFields(*gameObject);

				ImGui::Separator();

				const std::vector<ComponentBase*> components = gameObject->GetComponents();

				if (components.empty())
					ImGui::TextDisabled("No components.");

				for (ComponentBase* component : components)
					DrawComponent(gameObject, component);

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

	void Editor::DrawComponent(GameObject* gameObject, ComponentBase* component)
	{
		ImGui::PushID(component);

		const std::string name = ComponentRegistry::NameOf(*component);

		const bool open = ImGui::CollapsingHeader(
			name.c_str(),
			ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

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
		DrawFields(*component);

		component->OnGui();

		ImGui::PopID();
	};

	void Editor::DrawFields(LoomObject& object)
	{
		// A negative width leaves that many pixels for the label ImGui draws
		// after the widget; -FLT_MIN would leave none at all.
		ImGui::PushItemWidth(-160.0f);

		const std::vector<SerializedField>& fields = object.GetFields();

		for (size_t i = 0; i < fields.size(); i++)
		{
			const SerializedField& field = fields[i];

			ImGui::PushID(&field);

			// A field is its position, so that is all there is to call it.
			const std::string name = '#' + std::to_string(i);
			const char* label = name.c_str();

			switch (field.type)
			{
			case FieldType::Bool:
				ImGui::Checkbox(label, (bool*)field.data);
				break;

			case FieldType::Int:
				ImGui::DragScalar(label, ImGuiDataType_S32, field.data);
				break;

			case FieldType::UInt:
				ImGui::DragScalar(label, ImGuiDataType_U32, field.data);
				break;

			case FieldType::Int64:
				ImGui::DragScalar(label, ImGuiDataType_S64, field.data);
				break;

			case FieldType::UInt64:
				ImGui::DragScalar(label, ImGuiDataType_U64, field.data);
				break;

			case FieldType::Float:
				ImGui::DragFloat(label, (float*)field.data, 0.01f);
				break;

			case FieldType::Double:
				ImGui::DragScalar(label, ImGuiDataType_Double, field.data, 0.01f);
				break;

			case FieldType::String:
				InputTextString(label, *(std::string*)field.data);
				break;

			case FieldType::Vec2:
				ImGui::DragFloat2(label, (float*)field.data, 0.01f);
				break;

			case FieldType::Vec3:
				ImGui::DragFloat3(label, (float*)field.data, 0.01f);
				break;

			case FieldType::Vec4:
				ImGui::DragFloat4(label, (float*)field.data, 0.01f);
				break;

			case FieldType::FloatArray:
			{
				// Vertex data: worth seeing the size of, not worth 3000 drag boxes.
				const std::vector<float>& values = *(std::vector<float>*)field.data;

				ImGui::Text("%s: %d floats", label, (int)values.size());
			}
			break;

			case FieldType::Reference:
				DrawReferenceField(label, field);
				break;
			};

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

			if (m_activeScene == nullptr)
				ImGui::TextDisabled("No scene open. Pick one in the Project panel.");
			else if (available.x >= 1.0f && available.y >= 1.0f)
			{
				// Rendering here is fine mid-frame: ImGui only records the
				// texture id now and draws with it once the frame is submitted.
				m_viewport.Render(m_activeScene, (int)available.x, (int)available.y);

				if (m_viewport.GetTexture())
					ImGui::Image(
						m_viewport.GetTextureID(),
						ImVec2(
							(float)m_viewport.GetWidth(),
							(float)m_viewport.GetHeight()),
						ImVec2(0.0f, 1.0f),	// The framebuffer is bottom-up,
						ImVec2(1.0f, 0.0f));// so the V axis is flipped
			};
		};

		ImGui::End();
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
							ImGui::PushStyleColor(
								ImGuiCol_Text,
								ImVec4(0.85f, 0.25f, 0.25f, 1.0f));

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

			constexpr float MillisecondsPerSecond = 1000.0f;

			// ImGui already averages Framerate over its last 60 frames.
			ImGui::Text(
				"%d FPS (%d ms/frame)",
				(int)std::ceil(io.Framerate),
				(int)std::ceil(MillisecondsPerSecond / io.Framerate));

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
