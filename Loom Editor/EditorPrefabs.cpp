#include "Editor.h"

#include "Engine.h"
#include "Prefab.h"
#include "Prefabs.h"
#include "ProjectAssets.h"
#include "Scene.h"
#include "SceneHistory.h"
#include "SceneSerializer.h"

#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>


namespace Loom
{
	namespace
	{
		// Where Save as Prefab puts what it saves, in the project folder.
		constexpr const char* prefabs_folder = "Prefabs";

		// Named after a GameObject whose name is no file name.
		constexpr const char* unnamed_prefab = "Prefab";

		// None inside another, which is that one's content. Null when in none.
		GameObject* OutermostInstance(GameObject& gameObject)
		{
			GameObject* outermost = nullptr;

			for (GameObject* at = &gameObject; at; at = at->GetParent())
				if (Prefabs::LinkOf(*at))
					outermost = at;

			return outermost;
		};
	};

	#pragma region Prefabs

	#pragma region Files

	std::filesystem::path Editor::PrefabFile(const std::string& path) const
	{
		return (std::filesystem::path(m_projectPath) / path).lexically_normal();
	};

	std::filesystem::path Editor::PrefabFileOf(const GameObject& gameObject) const
	{
		if (const Prefab* link = Prefabs::NearestLink(gameObject))
			return PrefabFile(*link->path);

		if (const GameObject* root = GetPrefabRoot(); root != nullptr && Hierarchy::Contains(*root, &gameObject))
			return PrefabFile(m_prefabPath);

		return { };
	};

	const Prefabs::ObjectNode* Editor::LoadPrefab(const std::string& path)
	{
		std::error_code code;
		const std::filesystem::file_time_type written = std::filesystem::last_write_time(PrefabFile(path), code);

		if (code)
			return nullptr;

		const auto loaded = m_prefabs.find(path);

		if (loaded != m_prefabs.end() && loaded->second.written == written)
			return &loaded->second.tree;

		std::ifstream file(PrefabFile(path));
		std::stringstream text;
		text << file.rdbuf();

		Prefabs::ObjectNode prefab;
		std::string error;

		if (!Prefabs::Parse(text.str(), prefab, &error))
		{
			std::cerr << "Prefab " << path << ": " << error << std::endl;
			return nullptr;
		};

		// Changed outside the editor, so the file is what shows from now on.
		if (loaded != m_prefabs.end())
		{
			std::erase_if(m_linkedInstances, [&](const auto& link) { return link.second == path; });
			m_reshowPrefab = m_reshowPrefab || path == m_prefabPath;
		};

		LoadedPrefab& entry = m_prefabs[path];
		entry = LoadedPrefab{ std::move(prefab), written };

		return &entry.tree;
	};

	bool Editor::WritePrefab(const std::string& path, const Prefabs::ObjectNode& prefab)
	{
		const std::filesystem::path file = PrefabFile(path);

		std::error_code code;
		std::filesystem::create_directories(file.parent_path(), code);

		{
			std::ofstream out(file, std::ios::binary);
			out << Prefabs::Write(prefab);

			if (!out)
			{
				std::cerr << "Could not write prefab " << file.string() << std::endl;
				return false;
			};
		};

		m_prefabs[path] = LoadedPrefab{ prefab, std::filesystem::last_write_time(file, code) };

		return true;
	};

	bool Editor::IsInPrefab(const GameObject* gameObject, const std::string& path) const
	{
		if (path == m_prefabPath && m_prefabScene && gameObject && Hierarchy::Contains(m_prefabScene->GetRoot(), gameObject))
			return true;

		for (const GameObject* at = gameObject; at; at = at->GetParent())
			if (const Prefab* link = Prefabs::LinkOf(*at); link && *link->path == path)
				return true;

		return false;
	};

	#pragma endregion

	#pragma region Instances

	std::string Editor::SaveAsPrefab(GameObject* gameObject, const std::string& folder)
	{
		if (gameObject == nullptr || m_projectPath.empty())
			return "";

		const std::string name = ProjectAssets::IsValidName(gameObject->GetName()) ? gameObject->GetName() : unnamed_prefab;
		const std::filesystem::path into = folder.empty()
			? std::filesystem::path(m_projectPath) / prefabs_folder
			: std::filesystem::path(folder);

		std::filesystem::path file = into / (name + Prefab::extension);
		std::error_code code;

		for (int copy = 2; std::filesystem::exists(file, code); copy++)
			file = into / (name + " " + std::to_string(copy) + Prefab::extension);

		const std::string path = ProjectAssets::RelativeTo(file.string(), m_projectPath);

		Prefabs::ObjectNode prefab;

		if (!Prefabs::Parse(SceneSerializer::Serialize(*gameObject), prefab))
			return "";

		std::erase_if(prefab.components,
			[](const Prefabs::ComponentNode& component) { return component.type == Prefabs::PrefabType(); });

		if (!WritePrefab(path, Prefabs::Isolated(prefab)))
			return "";

		Prefab* link = gameObject->GetComponent<Prefab>();

		if (link == nullptr)
			link = gameObject->Attach<Prefab>();

		link->path = path;
		m_historyDirty = true;

		m_selectedAsset = file.lexically_normal().string();
		m_revealSelectedAsset = true;
		RefreshProjectAssets();

		return file.string();
	};

	void Editor::UnpackPrefab(GameObject& gameObject)
	{
		Prefab* link = Prefabs::LinkOf(gameObject);

		if (link == nullptr)
			return;

		m_linkedInstances.erase({ link->GetGuid().ToString(), *link->path });
		gameObject.DetachComponent(link);
		m_historyDirty = true;
	};

	GameObject* Editor::Instantiate(const std::string& path, GameObject& parent)
	{
		const Prefabs::ObjectNode* prefab = LoadPrefab(path);

		if (prefab == nullptr)
		{
			std::cerr << "There is no prefab at " << path << std::endl;
			return nullptr;
		};

		std::string error;
		GameObject* instance = SceneSerializer::Deserialize(Prefabs::Write(*prefab), parent, &error);

		if (instance == nullptr)
		{
			std::cerr << "Could not place prefab " << path << ": " << error << std::endl;
			return nullptr;
		};

		instance->Attach<Prefab>()->path = path;
		m_historyDirty = true;

		Select(instance);

		return instance;
	};

	#pragma endregion

	#pragma region Syncing

	void Editor::ReshowPrefab()
	{
		GameObject* root = GetPrefabRoot();
		const Prefabs::ObjectNode* prefab = root ? LoadPrefab(m_prefabPath) : nullptr;
		Prefabs::ObjectNode shown;

		if (prefab == nullptr || !Prefabs::Parse(SceneSerializer::Serialize(*root), shown))
			return;

		if (!ShowPrefab(Prefabs::Write(Prefabs::Retarget(*prefab, shown, Prefabs::TransformKeys(*root), Prefabs::Part::Whole))))
			return;

		// Agreed again on the next pass, from the copy as it now serializes.
		m_prefabShape.clear();
	};

	bool Editor::ShowPrefab(const std::string& subtree)
	{
		GameObject* root = GetPrefabRoot();

		if (root == nullptr)
			return false;

		const std::string text = Prefabs::Splice(SceneSerializer::Serialize(*m_prefabScene), root->GetGuid().ToString(), subtree);

		const Guid selected = IsSelectedIn(m_prefabScene->GetRoot()) ? m_selected->GetGuid() : Guid();

		if (selected != Guid())
			Select(nullptr);

		std::string error;
		m_prefabScene = SceneHistory::Replace(m_prefabScene, text, &error);

		if (m_prefabScene == nullptr)
		{
			std::cerr << "Could not show prefab " << m_prefabPath << " again: " << error << std::endl;
			m_prefabRoot = Guid();
			m_prefabPath.clear();
			m_prefabUnsaved = false;
			return false;
		};

		Scene::Unlist(m_prefabScene);
		Engine::DoTasks();

		m_historyDirty = true;

		if (GameObject* again = LoomObject::GetByGuid<GameObject>(selected))
			Select(again);

		return true;
	};

	void Editor::ReopenPrefab(const std::string& path, const std::string& held, const std::string& shape)
	{
		OpenPrefab(path);

		// The same prefab as before the build, whose steps still apply.
		m_prefabOpened = false;

		if (held.empty() || GetPrefabRoot() == nullptr)
			return;

		Engine::DoTasks();

		if (ShowPrefab(held))
			m_prefabShape = shape;
	};

	void Editor::HoldPrefabEdits()
	{
		m_prefabHeldBeforeBuild.clear();
		m_prefabShapeBeforeBuild = m_prefabShape;

		GameObject* root = GetPrefabRoot();
		Prefabs::ObjectNode shown;

		if (root == nullptr || m_prefabShape.empty() || !Prefabs::Parse(SceneSerializer::Serialize(*root), shown))
			return;

		if (Prefabs::Shape(shown, Prefabs::TransformKeys(*root), Prefabs::Part::Whole) != m_prefabShape)
			m_prefabHeldBeforeBuild = Prefabs::Write(shown);
	};

	void Editor::StepPrefab(bool undo)
	{
		if (!CanStepHistory())
			return;

		// So an edit not kept yet is the one taken back.
		RecordHistory();

		std::string text;

		if (!m_prefabHistory.Step(text, undo) || !ShowPrefab(text))
			return;

		if (GameObject* root = GetPrefabRoot())
			m_prefabHistory.Amend(SceneSerializer::Serialize(*root));
	};

	bool Editor::PrefabHasKeyboard() const
	{
		return GetPrefabRoot() != nullptr && (m_prefabFocused || IsSelectedIn(m_prefabScene->GetRoot()));
	};

	void Editor::DrawPrefabQuestion()
	{
		constexpr const char* popup = "Unsaved Prefab";

		if (m_prefabNext && !ImGui::IsPopupOpen(popup))
			ImGui::OpenPopup(popup);

		if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		if (!m_prefabNext)
		{
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		};

		ImGui::Text("Save the changes to %s?", std::filesystem::path(m_prefabPath).filename().string().c_str());

		const bool save = ImGui::Button("Save");
		ImGui::SameLine();
		const bool discard = ImGui::Button("Don't Save");
		ImGui::SameLine();
		const bool cancel = ImGui::Button("Cancel");

		// A save that fails leaves the question up, with the edits still held.
		const bool saved = save && SavePrefab();

		if (saved || discard)
		{
			const std::string next = *m_prefabNext;

			m_prefabUnsaved = false;

			if (next.empty())
			{
				m_showPrefab = false;
				ClosePrefab();
			}
			else OpenPrefab(next);
		};

		if (saved || discard || cancel)
		{
			m_prefabNext.reset();
			ImGui::CloseCurrentPopup();
		};

		ImGui::EndPopup();
	};

	bool Editor::PrefabsNeedSync() const
	{
		if (m_savePrefab || m_reshowPrefab || !ChangedPrefabFiles().empty())
			return true;

		GameObject* root = GetPrefabRoot();

		return root && (m_prefabShape.empty() || SceneSerializer::Serialize(*root) != m_panelText);
	};

	std::vector<std::string> Editor::ChangedPrefabFiles() const
	{
		std::vector<std::string> changed{ };

		for (const auto& [path, loaded] : m_prefabs)
		{
			std::error_code code;
			const std::filesystem::file_time_type written = std::filesystem::last_write_time(PrefabFile(path), code);

			// A prefab deleted or renamed away leaves SyncPrefabs nothing to load.
			if (!code && written != loaded.written)
				changed.push_back(path);
		};

		return changed;
	};

	void Editor::SyncPrefabs(const std::unordered_set<GameObject*>& changed)
	{
		if (m_projectPath.empty())
			return;

		const std::vector<std::string> written = ChangedPrefabFiles();
		std::set<std::string> paths(written.begin(), written.end());
		std::unordered_set<GameObject*> edited{ };

		for (GameObject* gameObject : changed)
		{
			GameObject* instance = OutermostInstance(*gameObject);
			Scene* scene = instance ? m_history.SceneOf(*instance) : nullptr;

			if (scene == nullptr)
				continue;

			const std::string& path = *Prefabs::LinkOf(*instance)->path;

			edited.insert(instance);
			m_instances[path].insert({ scene, instance->GetGuid() });
			paths.insert(path);
		};

		bool reshown = false;

		if (GetPrefabRoot())
		{
			LoadPrefab(m_prefabPath);

			if (std::exchange(m_reshowPrefab, false))
			{
				ReshowPrefab();
				reshown = true;
			};
		};

		GameObject* root = GetPrefabRoot();
		Prefabs::ObjectNode shown;
		const std::string panel_text = root ? SceneSerializer::Serialize(*root) : std::string();
		const bool showing = root && Prefabs::Parse(panel_text, shown);

		// A copy just shown agrees with its file as it now serializes.
		if (showing && m_prefabShape.empty())
			m_prefabShape = Prefabs::Shape(shown, Prefabs::TransformKeys(*root), Prefabs::Part::Whole);

		// An edit in the panel stays there until saved, for the instances to sync without it.
		const bool held = showing && !m_savePrefab && Prefabs::Shape(shown, Prefabs::TransformKeys(*root), Prefabs::Part::Whole) != m_prefabShape;

		if (showing && (m_savePrefab || panel_text != m_panelText))
			paths.insert(m_prefabPath);

		bool reshow = false;

		for (const std::string& path : paths)
		{
			const Prefabs::ObjectNode* prefab = LoadPrefab(path);

			if (prefab == nullptr)
				continue;

			const bool panel = showing && !held && path == m_prefabPath;

			std::vector<GameObject*> mine{ };
			std::vector<Prefabs::Instance> instances{ };
			std::vector<std::pair<std::string, std::string>> links{ };
			std::set<std::pair<Scene*, Guid>>& known = m_instances[path];

			for (auto at = known.begin(); at != known.end();)
			{
				const auto [scene, guid] = *at;
				const bool open = std::find(m_ownedScenes.begin(), m_ownedScenes.end(), scene) != m_ownedScenes.end();
				GameObject* instance = open ? m_history.Find(*scene, guid) : nullptr;
				const Prefab* link = instance ? Prefabs::LinkOf(*instance) : nullptr;
				auto read = m_readInstances.find(guid);

				if (link == nullptr || *link->path != path || OutermostInstance(*instance) != instance)
				{
					at = known.erase(at);
					continue;
				};

				at++;

				// Left unread when it fails, so the next pass tries again.
				if (read == m_readInstances.end() || edited.contains(instance))
				{
					Prefabs::ObjectNode tree;

					if (!Prefabs::Parse(SceneSerializer::Serialize(*instance), tree))
					{
						m_readInstances.erase(guid);
						continue;
					};

					read = m_readInstances.insert_or_assign(guid, std::move(tree)).first;
				};

				mine.push_back(instance);
				links.push_back({ link->GetGuid().ToString(), path });
				instances.push_back({ read->second, m_linkedInstances.contains(links.back()) });
			};

			const GameObject* any = panel ? root : mine.empty() ? nullptr : mine.front();

			if (any == nullptr)
				continue;

			const Prefabs::Outcome outcome = Prefabs::Sync(*prefab, panel ? &shown : nullptr, m_prefabShape, instances, Prefabs::TransformKeys(*any));

			if (outcome.prefab && WritePrefab(path, *outcome.prefab) && panel && !outcome.reshow)
				m_prefabShape = Prefabs::Shape(shown, Prefabs::TransformKeys(*root), Prefabs::Part::Whole);

			reshow = reshow || outcome.reshow;

			for (size_t i = 0; i < mine.size(); i++)
			{
				m_linkedInstances.insert(links[i]);

				if (!outcome.instances[i])
					continue;

				m_readInstances.erase(mine[i]->GetGuid());
				m_history.Rewrite(*mine[i], Prefabs::Write(*outcome.instances[i]));
			};
		};

		m_prefabUnsaved = showing && !reshow && Prefabs::Shape(shown, Prefabs::TransformKeys(*root), Prefabs::Part::Whole) != m_prefabShape;

		if (reshow)
			ReshowPrefab();

		root = GetPrefabRoot();
		m_panelText = root ? SceneSerializer::Serialize(*root) : std::string();

		if (root == nullptr)
			return;

		if (std::exchange(m_prefabOpened, false))
			m_prefabHistory.Reset(m_panelText);
		else if (reshow || reshown)
			m_prefabHistory.Amend(m_panelText);
		else m_prefabHistory.Keep(m_panelText);
	};

	#pragma endregion

	#pragma endregion
};
