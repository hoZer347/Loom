#include "Editor.h"

#include "Prefab.h"
#include "Prefabs.h"
#include "ProjectAssets.h"
#include "Scene.h"
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

		Prefab* LinkOf(GameObject& gameObject)
		{
			Prefab* link = gameObject.GetComponent<Prefab>();

			return link && !link->path->empty() ? link : nullptr;
		};

		struct Instance
		{
			Scene* scene;
			GameObject* root;
			std::pair<std::string, std::string> link;
			Prefabs::ObjectNode tree;
		};

		// Every instance under gameObject, but none inside another, which is
		// that one's content.
		void FindInstances(Scene& scene, GameObject& gameObject, std::vector<Instance>& instances)
		{
			if (Prefab* link = LinkOf(gameObject))
			{
				Instance instance{ &scene, &gameObject, { link->GetGuid().ToString(), *link->path } };

				if (Prefabs::Parse(SceneSerializer::Serialize(gameObject), instance.tree))
					instances.push_back(std::move(instance));

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

	bool Editor::LoadPrefab(const std::string& path, Prefabs::ObjectNode& prefab)
	{
		if (const auto loaded = m_prefabs.find(path); loaded != m_prefabs.end())
		{
			prefab = loaded->second;
			return true;
		};

		std::ifstream file(PrefabFile(path));

		if (!file)
			return false;

		std::stringstream text;
		text << file.rdbuf();

		std::string error;

		if (!Prefabs::Parse(text.str(), prefab, &error))
		{
			std::cerr << "Prefab " << path << ": " << error << std::endl;
			return false;
		};

		m_prefabs[path] = prefab;

		return true;
	};

	bool Editor::WritePrefab(const std::string& path, const Prefabs::ObjectNode& prefab)
	{
		const std::filesystem::path file = PrefabFile(path);

		std::error_code code;
		std::filesystem::create_directories(file.parent_path(), code);

		std::ofstream out(file, std::ios::binary);
		out << Prefabs::Write(prefab);

		if (!out)
		{
			std::cerr << "Could not write prefab " << file.string() << std::endl;
			return false;
		};

		m_prefabs[path] = prefab;

		return true;
	};

	void Editor::ForgetPrefabs()
	{
		m_prefabs.clear();
		m_linkedInstances.clear();

		GameObject* root = GetPrefabRoot();
		Prefabs::ObjectNode prefab;

		if (root &&
			LoadPrefab(m_prefabPath, prefab) &&
			Prefabs::Shape(prefab, Prefabs::TransformKeys(*root), Prefabs::Part::Whole) != m_prefabShape)
			OpenPrefab(m_prefabPath);
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

		const std::string path = file.lexically_normal().lexically_relative(m_projectPath).generic_string();

		Prefabs::ObjectNode prefab;

		if (!Prefabs::Parse(SceneSerializer::Serialize(*gameObject), prefab))
			return "";

		std::erase_if(prefab.components,
			[](const Prefabs::ComponentNode& component) { return component.type == Prefabs::PrefabType(); });

		if (!WritePrefab(path, prefab))
			return "";

		Prefab* link = gameObject->GetComponent<Prefab>();

		if (link == nullptr)
			link = gameObject->Attach<Prefab>();

		link->path = path;
		m_linkedInstances.insert({ link->GetGuid().ToString(), path });
		m_historyDirty = true;

		std::cout << "Saved '" << gameObject->GetName() << "' as the prefab " << path << std::endl;

		m_selectedAsset = file.lexically_normal().string();
		m_revealSelectedAsset = true;
		RefreshProjectAssets();

		return file.string();
	};

	GameObject* Editor::Instantiate(const std::string& path, GameObject& parent)
	{
		Prefabs::ObjectNode prefab;

		if (!LoadPrefab(path, prefab))
		{
			std::cerr << "There is no prefab at " << path << std::endl;
			return nullptr;
		};

		std::string error;
		GameObject* instance = SceneSerializer::Deserialize(Prefabs::Write(prefab), parent, &error);

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

	void Editor::SyncPrefabs()
	{
		if (m_projectPath.empty())
			return;

		// The prefabs whose files changed this pass, and whether the Prefab
		// panel is what changed one, which then wins over its instances.
		std::set<std::string> edited{ };
		bool from_panel = false;

		if (GameObject* root = GetPrefabRoot())
		{
			const std::vector<std::string> keys = Prefabs::TransformKeys(*root);
			Prefabs::ObjectNode shown;
			Prefabs::ObjectNode prefab;

			if (Prefabs::Parse(SceneSerializer::Serialize(*root), shown) && LoadPrefab(m_prefabPath, prefab))
			{
				std::string shape = Prefabs::Shape(shown, keys, Prefabs::Part::Whole);

				if (shape != m_prefabShape &&
					WritePrefab(m_prefabPath, Prefabs::Retarget(shown, prefab, keys, Prefabs::Part::Whole)))
				{
					m_prefabShape = std::move(shape);
					edited.insert(m_prefabPath);
					from_panel = true;
				};
			};
		};

		std::vector<Instance> instances{ };

		for (Scene* scene : m_ownedScenes)
			FindInstances(*scene, scene->GetRoot(), instances);

		// An instance known to agree with its prefab that no longer does has
		// been edited, and the prefab takes the edit.
		for (const Instance& instance : instances)
		{
			Prefabs::ObjectNode prefab;

			if (edited.contains(instance.link.second) ||
				!m_linkedInstances.contains(instance.link) ||
				!LoadPrefab(instance.link.second, prefab))
				continue;

			const std::vector<std::string> keys = Prefabs::TransformKeys(*instance.root);

			if (Prefabs::Shape(instance.tree, keys, Prefabs::Part::Shared) == Prefabs::Shape(prefab, keys, Prefabs::Part::Shared))
				continue;

			if (WritePrefab(instance.link.second, Prefabs::Retarget(instance.tree, prefab, keys, Prefabs::Part::Shared)))
				edited.insert(instance.link.second);
		};

		// Then every instance that differs from its prefab is brought into
		// line, a scene at a time.
		std::map<Scene*, std::string> texts{ };

		for (const Instance& instance : instances)
		{
			Prefabs::ObjectNode prefab;

			if (!LoadPrefab(instance.link.second, prefab))
				continue;

			m_linkedInstances.insert(instance.link);

			const std::vector<std::string> keys = Prefabs::TransformKeys(*instance.root);

			if (Prefabs::Shape(instance.tree, keys, Prefabs::Part::Shared) == Prefabs::Shape(prefab, keys, Prefabs::Part::Shared))
				continue;

			const auto [text, first] = texts.try_emplace(instance.scene);

			if (first)
				text->second = SceneSerializer::Serialize(*instance.scene);

			text->second = Prefabs::Splice(
				text->second,
				instance.root->GetGuid().ToString(),
				Prefabs::Write(Prefabs::Retarget(prefab, instance.tree, keys, Prefabs::Part::Shared)));
		};

		for (const auto& [scene, text] : texts)
			ReplaceScene(scene, text);

		// An instance's edit shows in the Prefab panel too.
		if (!from_panel && edited.contains(m_prefabPath))
			OpenPrefab(m_prefabPath);
	};
};
