#include "Editor.h"

#include "Engine.h"
#include "Prefab.h"
#include "Prefabs.h"
#include "ProjectAssets.h"
#include "Scene.h"
#include "SceneHistory.h"
#include "SceneSerializer.h"

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

		struct Found
		{
			Scene* scene;
			GameObject* root;
			std::pair<std::string, std::string> link;
			Prefabs::ObjectNode tree;
		};

		// Every instance under gameObject, but none inside another, which is
		// that one's content.
		void FindInstances(Scene& scene, GameObject& gameObject, std::vector<Found>& instances)
		{
			if (Prefab* link = Prefabs::LinkOf(gameObject))
			{
				Found found{ &scene, &gameObject, { link->GetGuid().ToString(), *link->path } };

				if (Prefabs::Parse(SceneSerializer::Serialize(gameObject), found.tree))
					instances.push_back(std::move(found));

				return;
			};

			for (GameObject* child : gameObject.GetChildren())
				FindInstances(scene, *child, instances);
		};
	};

	std::filesystem::path Editor::PrefabFile(const std::string& path) const
	{
		return (std::filesystem::path(m_projectPath) / path).lexically_normal();
	};

	std::filesystem::path Editor::PrefabFileOf(const GameObject& gameObject) const
	{
		if (const Prefab* link = Prefabs::NearestLink(gameObject))
			return PrefabFile(*link->path);

		if (const GameObject* root = GetPrefabRoot(); root != nullptr && IsInHierarchy(*root, &gameObject))
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

		// Changed outside the editor: the file is what instances and the panel
		// show from now on.
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
		if (path == m_prefabPath && m_prefabScene && gameObject && IsInHierarchy(m_prefabScene->GetRoot(), gameObject))
			return true;

		for (const GameObject* at = gameObject; at; at = at->GetParent())
			if (const Prefab* link = Prefabs::LinkOf(*at); link && *link->path == path)
				return true;

		return false;
	};

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

		std::cout << "Saved '" << gameObject->GetName() << "' as the prefab " << path << std::endl;

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

	void Editor::ReshowPrefab()
	{
		GameObject* root = GetPrefabRoot();
		const Prefabs::ObjectNode* prefab = root ? LoadPrefab(m_prefabPath) : nullptr;
		Prefabs::ObjectNode shown;

		if (prefab == nullptr || !Prefabs::Parse(SceneSerializer::Serialize(*root), shown))
			return;

		const std::string text = Prefabs::Splice(
			SceneSerializer::Serialize(*m_prefabScene),
			root->GetGuid().ToString(),
			Prefabs::Write(Prefabs::Retarget(*prefab, shown, Prefabs::TransformKeys(*root), Prefabs::Part::Whole)));

		const Guid selected = m_selected && IsInHierarchy(m_prefabScene->GetRoot(), m_selected) ? m_selected->GetGuid() : Guid();

		if (selected != Guid())
			Select(nullptr);

		std::string error;
		m_prefabScene = SceneHistory::Replace(m_prefabScene, text, &error);

		if (m_prefabScene == nullptr)
		{
			std::cerr << "Could not show prefab " << m_prefabPath << " again: " << error << std::endl;
			m_prefabRoot = Guid();
			m_prefabPath.clear();
			return;
		};

		Scene::Unlist(m_prefabScene);
		Engine::DoTasks();

		// Agreed again on the next pass, from the copy as it now serializes.
		m_prefabShape.clear();
		m_historyDirty = true;

		if (GameObject* again = LoomObject::GetByGuid<GameObject>(selected))
			Select(again);
	};

	bool Editor::PrefabsNeedSync()
	{
		if (m_reshowPrefab || (GetPrefabRoot() && m_prefabShape.empty()))
			return true;

		for (const auto& [path, loaded] : m_prefabs)
		{
			std::error_code code;

			if (std::filesystem::last_write_time(PrefabFile(path), code) != loaded.written)
				return true;
		};

		return false;
	};

	void Editor::SyncPrefabs()
	{
		if (m_projectPath.empty())
			return;

		std::vector<Found> found{ };

		for (Scene* scene : m_ownedScenes)
			FindInstances(*scene, scene->GetRoot(), found);

		if (GetPrefabRoot())
		{
			LoadPrefab(m_prefabPath);

			if (std::exchange(m_reshowPrefab, false))
				ReshowPrefab();
		};

		GameObject* root = GetPrefabRoot();
		const GameObject* any = root ? root : found.empty() ? nullptr : found.front().root;

		if (any == nullptr)
			return;

		const std::vector<std::string> keys = Prefabs::TransformKeys(*any);

		Prefabs::ObjectNode shown;
		const bool showing = root && Prefabs::Parse(SceneSerializer::Serialize(*root), shown);

		// A copy just shown agrees with its file as it now serializes.
		if (showing && m_prefabShape.empty())
			m_prefabShape = Prefabs::Shape(shown, keys, Prefabs::Part::Whole);

		std::set<std::string> paths{ };

		for (const Found& instance : found)
			paths.insert(instance.link.second);

		if (showing)
			paths.insert(m_prefabPath);

		std::map<Scene*, std::string> texts{ };
		bool reshow = false;

		for (const std::string& path : paths)
		{
			const Prefabs::ObjectNode* prefab = LoadPrefab(path);

			if (prefab == nullptr)
				continue;

			std::vector<const Found*> mine{ };
			std::vector<Prefabs::Instance> instances{ };

			for (const Found& instance : found)
				if (instance.link.second == path)
				{
					mine.push_back(&instance);
					instances.push_back({ instance.tree, m_linkedInstances.contains(instance.link) });
				};

			const bool panel = showing && path == m_prefabPath;
			const Prefabs::Outcome outcome = Prefabs::Sync(*prefab, panel ? &shown : nullptr, m_prefabShape, instances, keys);

			if (outcome.prefab && WritePrefab(path, *outcome.prefab) && panel && !outcome.reshow)
				m_prefabShape = Prefabs::Shape(shown, keys, Prefabs::Part::Whole);

			reshow = reshow || outcome.reshow;

			for (size_t i = 0; i < mine.size(); i++)
			{
				m_linkedInstances.insert(mine[i]->link);

				if (!outcome.instances[i])
					continue;

				const auto [text, first] = texts.try_emplace(mine[i]->scene);

				if (first)
					text->second = SceneSerializer::Serialize(*mine[i]->scene);

				text->second = Prefabs::Splice(
					text->second,
					mine[i]->root->GetGuid().ToString(),
					Prefabs::Write(*outcome.instances[i]));
			};
		};

		for (const auto& [scene, text] : texts)
			ReplaceScene(scene, text);

		if (reshow)
			ReshowPrefab();
	};
};
