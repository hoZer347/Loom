#include "Editor.h"

#include "EditorFileDialogs.h"
#include "ProjectTemplate.h"

#include "Engine.h"
#include "GameObject.h"
#include "Scene.h"
#include "SceneSerializer.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>


namespace Loom
{
	namespace
	{
		// Under the project's build folder: the scene Play Web hands over, and
		// the page and module built beside it.
		const char* const web_folder = "Web";
		const char* const web_scene = "Scene";

		// A path from a .loomproject is relative to the project folder.
		std::string Resolve(const std::string& folder, const std::string& path)
		{
			if (path.empty())
				return "";

			const std::filesystem::path relative(path);

			return relative.is_absolute()
				? relative.lexically_normal().string()
				: (std::filesystem::path(folder) / relative).lexically_normal().string();
		};
	};

	void Editor::LoadProjectFile()
	{
		m_scripts.Clear();
		m_projectFile.clear();

		// Unless the project file names it otherwise.
		m_projectName = std::filesystem::path(m_projectPath).filename().string();

		std::error_code code;

		for (const auto& entry :
			std::filesystem::directory_iterator(m_projectPath, code))
		{
			if (entry.is_regular_file(code) &&
				entry.path().extension() == ProjectTemplate::extension)
			{
				m_projectFile = entry.path().lexically_normal().string();
				break;
			};
		};

		if (m_projectFile.empty())
			return;

		std::ifstream in(m_projectFile);
		std::string line, project, library;

		while (std::getline(in, line))
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
				line.pop_back();

			if (const std::string value = ProjectTemplate::ValueOf(line, ProjectTemplate::name_key); !value.empty())
				m_projectName = value;

			if (const std::string value = ProjectTemplate::ValueOf(line, ProjectTemplate::scripts_key); !value.empty())
				project = Resolve(m_projectPath, value);

			if (const std::string value = ProjectTemplate::ValueOf(line, ProjectTemplate::library_key); !value.empty())
				library = Resolve(m_projectPath, value);
		};

		if (project.empty())
			return;

		m_scripts.SetProject(project, library);

		std::cout
			<< "Project '" << m_projectName << "' scripts: " << project << std::endl;

		// Loaded before any scene opens: a scene that mentions a script type
		// needs that type to exist by the time it is read. A library that has
		// fallen behind its sources, or behind the editor, is rebuilt first;
		// PollScripts opens the scene once that finishes.
		if (m_scripts.IsStale())
		{
			std::cout << "Scripts are out of date; rebuilding." << std::endl;

			CompileScripts();
		}
		else if (!m_scripts.Load())
		{
			// Never built, or a copy of it is in the way. Opening the scenes now
			// would drop every component that comes from the library, and the
			// next save would write them out of the file for good, so this does
			// what pressing Compile does and waits for the result.
			std::cout
				<< "Scripts did not load (" << m_scripts.GetStatus()
				<< "); building them before opening anything." << std::endl;

			CompileScripts();
		};
	};

	bool Editor::CreateProject(const std::string& folder, const std::string& name)
	{
		std::string error;

		const std::string project_file = ProjectTemplate::Create(folder, name, &error);

		if (project_file.empty())
		{
			std::cerr << "Could not create the project: " << error << std::endl;
			return false;
		};

		// The scenes wait until the scripts they refer to have been built.
		if (!OpenProject(std::filesystem::path(project_file).parent_path().string(), false))
			return false;

		CompileScripts();

		return true;
	};

	void Editor::CompileScripts()
	{
		if (!m_scripts.HasProject())
		{
			std::cerr << "This project has no scripts project to compile." << std::endl;
			return;
		};

		if (m_scripts.IsBuilding())
			return;

		// The components in the open scenes are about to have the code behind
		// them unloaded, so the scenes go out as text and come back the same way.
		m_snapshots = TakeSnapshots();
		m_selectedBeforeBuild = m_selected ? m_selected->GetGuid() : Guid();

		CloseAllScenes();

		m_scripts.Unload();

		if (!m_scripts.Build())
			RestoreAfterBuild();
	};

	void Editor::RestoreAfterBuild()
	{
		RestoreSnapshots(m_snapshots);
		m_snapshots.clear();

		if (GameObject* selected = LoomObject::GetByGuid<GameObject>(m_selectedBeforeBuild))
			Select(selected);
	};

	void Editor::CloseAllScenes()
	{
		const std::vector<Scene*> closing = m_ownedScenes;

		m_ownedScenes.clear();
		m_scenePaths.clear();
		m_activeScene = nullptr;
		Select(nullptr);

		// Anything the hierarchy still had queued has to land before the
		// objects it refers to are deleted.
		Engine::DoTasks();

		for (Scene* scene : closing)
			delete scene;

		Engine::DoTasks();
	};

	void Editor::PollScripts()
	{
		if (m_scripts.Poll() != ScriptLibrary::Progress::Finished)
			return;

		if (m_scripts.LastBuildSucceeded())
			m_scripts.Load();

		RestoreAfterBuild();

		RefreshProjectAssets();

		// Nothing open yet: the scene this project starts on was waiting for the
		// scripts it mentions.
		if (m_ownedScenes.empty())
			if (const std::string scene = StartupScene(); !scene.empty())
				OpenScene(scene);

		if (m_playAfterCompile)
		{
			m_playAfterCompile = false;

			if (m_scripts.LastBuildSucceeded())
				BeginPlay();
		};
	};

	std::vector<Editor::SceneSnapshot> Editor::TakeSnapshots() const
	{
		// A scene is only finished being built once the queue has run: AddChild
		// and Attach defer, so serializing before that writes out a hierarchy
		// that is still missing its children.
		Engine::DoTasks();

		std::vector<SceneSnapshot> snapshots;

		for (Scene* scene : m_ownedScenes)
		{
			const auto path = m_scenePaths.find(scene);

			snapshots.push_back(
				SceneSnapshot{
					SceneSerializer::Serialize(*scene),
					path != m_scenePaths.end() ? path->second : std::string(),
					scene == m_activeScene });
		};

		return snapshots;
	};

	void Editor::RestoreSnapshots(const std::vector<SceneSnapshot>& snapshots)
	{
		// Resolved once every scene is back, since a reference can point into a
		// scene restored after the one holding it.
		SceneSerializer::PendingReferences pending;

		for (const SceneSnapshot& snapshot : snapshots)
		{
			std::string error;

			Scene* scene = SceneSerializer::Deserialize(snapshot.text, &error, &pending);

			if (scene == nullptr)
			{
				std::cerr << "Could not put a scene back: " << error << std::endl;
				continue;
			};

			m_ownedScenes.push_back(scene);
			m_scenePaths[scene] = snapshot.path;

			if (snapshot.active || m_activeScene == nullptr)
			{
				SetActiveScene(scene);

				if (m_selectedModel.empty())
					Select(&scene->GetRoot());
			};
		};

		SceneSerializer::ResolveReferences(pending);
	};

	void Editor::TogglePlay()
	{
		if (Engine::updateScenes)
		{
			// A play left queued by an earlier build goes with it: a pause
			// should not be undone by the next compile finishing.
			Engine::updateScenes = false;
			m_playAfterCompile = false;

			return;
		};

		// A run starts from freshly compiled scripts. A paused one carries on
		// with the library it started with.
		if (!m_playing && m_scripts.HasProject() && !m_scripts.IsBuilding())
		{
			m_playAfterCompile = true;
			CompileScripts();
			return;
		};

		BeginPlay();
	};

	void Editor::BeginPlay()
	{
		// Mid-build the scenes are down and there is nothing to snapshot or to
		// update: the run waits for the ones PollScripts puts back.
		if (m_scripts.IsBuilding())
		{
			m_playAfterCompile = true;
			return;
		};

		// Only the first Play of a session takes the snapshot: a paused run
		// carries on from where it stopped, and still goes back to the state it
		// started in rather than the state it was paused in.
		if (!m_playing)
		{
			m_playSnapshots = TakeSnapshots();
			m_playing = true;
		};

		Engine::updateScenes = true;
	};

	void Editor::Stop()
	{
		Engine::updateScenes = false;
		m_playAfterCompile = false;
		m_steppedLastFrame = false;

		if (!m_playing)
			return;

		m_playing = false;

		std::vector<SceneSnapshot> snapshots;

		snapshots.swap(m_playSnapshots);

		// Mid-build the scenes are already down, waiting on a library that is
		// not there yet. What PollScripts puts back becomes the base scene.
		if (m_scripts.IsBuilding())
		{
			m_snapshots = std::move(snapshots);

			std::cout << "Stopped; the scenes reset once the build lands." << std::endl;

			return;
		};

		CloseAllScenes();
		RestoreSnapshots(snapshots);

		std::cout << "Stopped; scenes reset." << std::endl;
	};

	bool Editor::IsPlaying() const
	{
		// A run queued behind a build counts as one: Stop is how it is called off.
		return m_playing || m_playAfterCompile;
	};

	void Editor::PlayWeb()
	{
		if (m_activeScene == nullptr || m_projectPath.empty())
		{
			std::cerr << "Play Web needs a project folder and a scene open in it" << std::endl;
			return;
		};

		std::string text = SceneSerializer::Serialize(*m_activeScene);

		for (const SceneSnapshot& snapshot : m_playSnapshots)
			if (snapshot.active)
				text = snapshot.text;

		const std::filesystem::path scene =
			std::filesystem::path(m_projectPath) /
			ProjectTemplate::build_folder /
			web_folder /
			(std::string(web_scene) + SceneSerializer::extension);

		std::error_code code;
		std::filesystem::create_directories(scene.parent_path(), code);

		std::ofstream out(scene, std::ios::binary);

		if (!(out << text))
		{
			std::cerr << "Could not write " << scene.string() << std::endl;
			return;
		};

		out.close();

		m_webPlayer.Launch(m_projectPath, scene.string());
	};

	void Editor::DrawNewProjectPrompt()
	{
		if (!m_askForNewProject)
			return;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		if (ImGui::Begin(
			"New Project",
			nullptr,
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::TextUnformatted("A folder of scenes, and a C++ project for the scripts.");
			ImGui::TextDisabled("Letters, digits, _ and - in the name; it becomes a project name.");

			ImGui::Separator();

			ImGui::SetNextItemWidth(420.0f);
			ImGui::InputText("Location", m_newProjectFolder, sizeof(m_newProjectFolder));

			ImGui::SameLine();

			if (ImGui::Button("Browse..."))
				m_dialogs->newProjectFolder.Open();

			ImGui::SetNextItemWidth(420.0f);
			ImGui::InputText("Name", m_newProjectName, sizeof(m_newProjectName));

			const bool usable =
				m_newProjectFolder[0] &&
				ProjectTemplate::IsValidName(m_newProjectName);

			ImGui::TextDisabled(
				"%s",
				usable
					? (std::filesystem::path(m_newProjectFolder) / m_newProjectName).string().c_str()
					: "Pick a folder and a usable name.");

			ImGui::BeginDisabled(!usable);

			if (ImGui::Button("Create", ImVec2(160.0f, 0.0f)))
				if (CreateProject(m_newProjectFolder, m_newProjectName))
					m_askForNewProject = false;

			ImGui::EndDisabled();

			ImGui::SameLine();

			if (ImGui::Button("Cancel", ImVec2(160.0f, 0.0f)))
				m_askForNewProject = false;
		};

		ImGui::End();

		// The browser is its own modal, so it is displayed from here rather than
		// from inside the prompt window.
		m_dialogs->newProjectFolder.Display();

		if (m_dialogs->newProjectFolder.HasSelected())
		{
			snprintf(
				m_newProjectFolder,
				sizeof(m_newProjectFolder),
				"%s",
				m_dialogs->newProjectFolder.GetSelected().string().c_str());

			m_dialogs->newProjectFolder.ClearSelected();
		};
	};
};
