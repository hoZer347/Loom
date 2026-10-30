#include "Editor.h"

#include "EditorFileDialogs.h"
#include "EditorIcons.h"
#include "ProjectTemplate.h"

#include "Audio.h"
#include "Engine.h"
#include "GameObject.h"
#include "Scene.h"
#include "SceneSerializer.h"

#include "Utilities/Clock.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <utility>


namespace Loom
{
	namespace
	{
		// Under the build folder: the scene Play Web hands over, and its page and module.
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

	#pragma region Project

	void Editor::LoadProjectFile()
	{
		const std::string previous = m_projectFile;

		m_scripts.Clear();
		m_projectFile.clear();
		m_componentMenu.Clear();
		m_scriptFolders.Clear();

		for (FileTab& tab : m_fileTabs)
			tab.folders.Clear();

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

		// The project open already, read again for its scripts project, keeps
		// the key bindings and build settings it has not saved yet.
		const bool reread = !m_projectFile.empty() && m_projectFile == previous;

		if (!reread)
		{
			InputActions::Set({ });
			m_keyBindings.Loaded();
			m_buildSettings = BuildSettings{ };
		};

		if (m_projectFile.empty())
			return;

		m_scriptFolders.Load(ScriptFoldersFile());

		for (FileTab& tab : m_fileTabs)
			tab.Load(FileFoldersFile(tab));

		if (!reread)
		{
			InputActions::Load(InputActionsFile());
			m_keyBindings.Loaded();
			m_buildSettings.Load(BuildSettingsFile());
		};

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

			m_componentMenu.Read(line);
		};

		if (project.empty())
			return;

		if (std::string error; !ProjectTemplate::Precompile(project, &error))
			std::cerr << error << std::endl;

		if (std::string error; !ProjectTemplate::CatchCrashes(project, &error))
			std::cerr << error << std::endl;

		m_scripts.SetProject(project, library);

		if (logScriptsProject)
			std::cout << "Project '" << m_projectName << "' scripts: " << project << std::endl;

		// A scene needs its script types before it is read; a stale library is rebuilt first.
		if (m_scripts.IsStale())
			CompileScripts();
		else if (!m_scripts.Load())
		{
			// Never built or blocked: opening now would show every script component as not loaded.
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

		// The running build began before the script to attach was written.
		if (m_scripts.IsBuilding())
		{
			m_compileAfterBuild = !m_attachAfterBuild.empty();
			return;
		};

		FinishOpening();

		m_projectLoad.reset();

		// The code behind the scenes' components is about to unload, so they go out as text.
		m_snapshots = TakeSnapshots();
		m_selectedBeforeBuild = GetSelectedGuids();
		m_prefabBeforeBuild = m_prefabPath;
		HoldPrefabEdits();

		// Ahead of the scenes, whose closing runs the queue that deletes it.
		ClosePrefab();
		CloseAllScenes();

		m_scripts.Unload();

		if (!m_scripts.Build())
		{
			m_attachAfterBuild.clear();
			m_attachAfterBuildTo.clear();
			m_attachAfterBuildInPrefab.clear();
			RestoreAfterBuild();
		};
	};

	void Editor::PollScripts()
	{
		if (m_scripts.Poll() != ScriptLibrary::Progress::Finished)
			return;

		if (m_scripts.LastBuildSucceeded())
			m_scripts.Load();

		// What opened mid-build holds MissingComponents, so it is reread on the new library.
		FinishOpening();

		std::vector<SceneSnapshot> opened = TakeSnapshots();

		if (!opened.empty())
		{
			m_snapshots.insert(m_snapshots.end(), std::make_move_iterator(opened.begin()), std::make_move_iterator(opened.end()));
			m_selectedBeforeBuild = GetSelectedGuids();
		};

		if (!m_prefabPath.empty())
		{
			m_prefabBeforeBuild = m_prefabPath;
			HoldPrefabEdits();
		};

		ClosePrefab();
		CloseAllScenes();

		RestoreAfterBuild();

		if (!m_compileAfterBuild)
			AttachAfterBuild();

		RefreshProjectAssets();

		if (m_ownedScenes.empty())
			if (const std::string scene = StartupScene(); !scene.empty())
				BeginOpeningScene(scene);

		if (m_playAfterCompile)
		{
			m_playAfterCompile = false;

			if (m_scripts.LastBuildSucceeded())
				BeginPlay();
		};

		if (std::exchange(m_compileAfterBuild, false))
			CompileScripts();
	};

	void Editor::HoldPrefabAttachTargets()
	{
		m_attachAfterBuildInPrefab.clear();
		m_attachAfterBuildPrefab = m_prefabPath;

		const GameObject* root = GetPrefabRoot();

		if (root == nullptr)
			return;

		std::erase_if(
			m_attachAfterBuildTo,
			[this, root](const Guid& guid)
			{
				const GameObject* object = LoomObject::GetByGuid<GameObject>(guid);

				if (object == nullptr || !Hierarchy::Contains(*root, object))
					return false;

				std::vector<size_t> indices{ };

				for (; object != root; object = object->GetParent())
				{
					const std::vector<GameObject*>& siblings = object->GetParent()->GetChildren();
					const size_t index = std::distance(siblings.begin(), std::ranges::find(siblings, object));

					indices.insert(indices.begin(), index);
				};

				m_attachAfterBuildInPrefab.push_back(std::move(indices));

				return true;
			});
	};

	void Editor::AttachAfterBuild()
	{
		const std::string name = std::exchange(m_attachAfterBuild, { });
		std::vector<GameObject*> objects{ };

		for (const Guid& guid : std::exchange(m_attachAfterBuildTo, { }))
			if (GameObject* object = LoomObject::GetByGuid<GameObject>(guid))
				objects.push_back(object);

		Engine::DoTasks();

		const bool in_prefab = !m_attachAfterBuildInPrefab.empty();

		for (const std::vector<size_t>& indices : std::exchange(m_attachAfterBuildInPrefab, { }))
		{
			GameObject* object = m_attachAfterBuildPrefab == m_prefabPath ? GetPrefabRoot() : nullptr;

			for (const size_t index : indices)
				object = object != nullptr && index < object->GetChildren().size()
					? object->GetChildren()[index]
					: nullptr;

			if (object != nullptr)
				objects.push_back(object);
		};

		if (name.empty())
			return;

		if (objects.empty())
		{
			std::cerr << name << " compiled, but what it was made for is gone, so it is on nothing." << std::endl;
			return;
		};

		// So the scenes the build restored are what an undo goes back to.
		RecordHistory();

		std::vector<GameObject*> missed{ };

		ChangeComponents(
			objects,
			[&](size_t i)
			{
				if (ComponentRegistry::Create(name, *objects[i]) == nullptr)
					missed.push_back(objects[i]);
			});

		m_historyDirty = true;

		// Reopening the prefab dropped its objects from the selection.
		if (in_prefab)
			SetSelection(objects);

		for (GameObject* object : missed)
			std::cerr << name << " did not compile, so it is not on " << object->GetName() << " yet." << std::endl;
	};

	#pragma endregion

	#pragma region Snapshots

	void Editor::RestoreAfterBuild()
	{
		RestoreSnapshots(m_snapshots);
		m_snapshots.clear();

		if (!m_prefabBeforeBuild.empty())
			ReopenPrefab(
				std::exchange(m_prefabBeforeBuild, { }),
				std::exchange(m_prefabHeldBeforeBuild, { }),
				std::exchange(m_prefabShapeBeforeBuild, { }));

		SelectAgain(m_selectedBeforeBuild);
	};

	std::vector<Editor::SceneSnapshot> Editor::TakeSnapshots() const
	{
		// AddChild and Attach defer, so serializing before the queue runs loses children.
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
		// After every scene is back, since a reference can point into a later one.
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

	void Editor::BeginPlay()
	{
		// Mid-build the scenes are down; the run waits for PollScripts or the one opening.
		if (m_scripts.IsBuilding() || m_opening)
		{
			m_playAfterCompile = true;
			return;
		};

		// A paused run still goes back to the state it started in.
		if (!m_playing)
		{
			m_playSnapshots = TakeSnapshots();
			m_playHistory.Clear();
			m_playEdits.clear();
			m_playing = true;
		};

		Engine::updateScenes = true;
	};

	#pragma endregion

	#pragma region Scenes

	void Editor::CloseAllScenes()
	{
		const std::vector<Scene*> closing = m_ownedScenes;

		m_ownedScenes.clear();
		m_scenePaths.clear();
		m_activeScene = nullptr;
		Select(nullptr);

		// Queued hierarchy work lands before the objects it refers to are deleted.
		Engine::DoTasks();

		for (Scene* scene : closing)
			delete scene;

		Engine::DoTasks();
	};

	#pragma endregion

	#pragma region Play

	void Editor::TogglePlay()
	{
		if (Engine::updateScenes)
		{
			// A pause should not be undone by the next compile finishing.
			Engine::updateScenes = false;
			m_playAfterCompile = false;

			return;
		};

		// A paused run keeps the library it started with.
		if (!m_playing && m_scripts.HasProject() && !m_scripts.IsBuilding())
		{
			m_playAfterCompile = true;
			CompileScripts();
			return;
		};

		BeginPlay();
	};

	void Editor::Stop()
	{
		Engine::updateScenes = false;
		m_playAfterCompile = false;
		m_steppedLastFrame = false;

		if (!m_playing)
			return;

		m_playing = false;

		// A script that slowed or stopped the clock does not hold the next run to it.
		Time::SetScale(1.0f);

		Audio::Unload();

		std::vector<SceneSnapshot> snapshots;

		snapshots.swap(m_playSnapshots);

		// Mid-build the scenes are down; what PollScripts puts back becomes the base.
		if (m_scripts.IsBuilding())
		{
			m_snapshots = std::move(snapshots);

			return;
		};

		CloseAllScenes();
		RestoreSnapshots(snapshots);
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

	#pragma endregion

	#pragma region Project Panel

	std::string Editor::BuildSettingsFile() const
	{
		return m_projectFile.empty()
			? ""
			: std::filesystem::path(m_projectFile).replace_extension(BuildSettings::extension).string();
	};

	bool Editor::SaveBuildSettings()
	{
		if (const std::string file = BuildSettingsFile(); !m_buildSettings.Save(file))
		{
			std::cerr << "Build Settings: could not write them to " << file << std::endl;
			return false;
		};

		return true;
	};

	std::vector<std::string> Editor::BuildScenes() const
	{
		std::vector<std::string> scenes{ };
		std::error_code code;

		for (auto entry = std::filesystem::recursive_directory_iterator(m_projectPath, code);
			entry != std::filesystem::recursive_directory_iterator();
			entry.increment(code))
		{
			const std::string name = entry->path().filename().string();

			if (entry->is_directory(code))
			{
				if (name == ProjectTemplate::build_folder || name.starts_with('.'))
					entry.disable_recursion_pending();
			}
			else if (entry->path().extension() == SceneSerializer::extension)
				scenes.push_back(entry->path().lexically_relative(m_projectPath).generic_string());
		};

		std::sort(scenes.begin(), scenes.end());

		return scenes;
	};

	void Editor::DrawBuildSettings()
	{
		if (m_focusBuildSettings)
		{
			ImGui::SetNextWindowFocus();
			m_focusBuildSettings = false;
		};

		// Starting on the scene being edited, until another is chosen.
		if (m_buildSettings.scene.empty() && !m_projectPath.empty())
			if (const auto path = m_scenePaths.find(m_activeScene); path != m_scenePaths.end() && !path->second.empty())
				m_buildSettings.DefaultScene(std::filesystem::path(path->second).lexically_relative(m_projectPath).generic_string());

		const GameBuilder::Status status = m_gameBuilder.GetStatus();

		const BuildSettings::Action action = m_buildSettings.Draw(
			&m_showBuildSettings,
			m_projectPath,
			m_projectName,
			[this]() { return BuildScenes(); },
			GameBuilder::Label(status),
			status == GameBuilder::Status::Building);

		switch (action)
		{
		case BuildSettings::Action::Save:
			SaveBuildSettings();
			break;

		case BuildSettings::Action::Browse:
			m_dialogs->buildFolder.SetPwd(std::filesystem::path(m_projectPath).parent_path());
			m_dialogs->buildFolder.Open();
			break;

		case BuildSettings::Action::Build:
			BuildGame(false);
			break;

		case BuildSettings::Action::BuildAndRun:
			BuildGame(true);
			break;

		default:
			break;
		};
	};

	void Editor::BuildGame(bool run)
	{
		m_gameBuilder.Launch(
			m_buildSettings.Arguments(m_projectPath, m_projectName),
			run ? m_buildSettings.Program(m_projectPath, m_projectName) : std::filesystem::path());
	};

	#pragma endregion

	#pragma region Dialogs

	void Editor::DrawNewProjectPrompt()
	{
		if (!m_askForNewProject)
			return;

		constexpr float field_width = 420.0f;
		constexpr float button_width = 160.0f;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		if (ImGui::Begin(
			(std::string(EditorIcons::Project) + " New Project").c_str(),
			nullptr,
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::SetNextItemWidth(field_width);
			ImGui::InputText("Location", m_newProjectFolder, sizeof(m_newProjectFolder));

			ImGui::SameLine();

			if (ImGui::Button("Browse..."))
				m_dialogs->newProjectFolder.Open();

			ImGui::SetNextItemWidth(field_width);
			ImGui::InputText("Name", m_newProjectName, sizeof(m_newProjectName));

			const bool usable =
				m_newProjectFolder[0] &&
				ProjectTemplate::IsValidName(m_newProjectName);

			ImGui::TextDisabled(
				"%s",
				usable
					? (std::filesystem::path(m_newProjectFolder) / m_newProjectName).string().c_str()
					: "");

			ImGui::BeginDisabled(!usable);

			if (ImGui::Button("Create", ImVec2(button_width, 0.0f)))
			{
				Stop();

				AskToSave(
					UnsavedWork(),
					[this]() { return SaveProject(); },
					[this, folder = std::string(m_newProjectFolder), name = std::string(m_newProjectName)]()
					{
						if (CreateProject(folder, name))
							m_askForNewProject = false;
					});
			};

			ImGui::EndDisabled();

			ImGui::SameLine();

			if (ImGui::Button("Cancel", ImVec2(button_width, 0.0f)))
				m_askForNewProject = false;
		};

		ImGui::End();

		// The browser is its own modal, so it is displayed outside the prompt window.
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

	#pragma endregion
};
