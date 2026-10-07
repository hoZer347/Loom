#include "SceneHistory.h"

#include "Engine.h"
#include "Scene.h"
#include "SceneSerializer.h"

#include <algorithm>


namespace Loom
{
	void SceneHistory::Record(const std::vector<Scene*>& scenes)
	{
		// AddChild and Destroy defer; the text has to show where they land.
		Engine::DoTasks();

		for (Scene* scene : scenes)
		{
			std::string text = SceneSerializer::Serialize(*scene);

			const auto [last, added] = m_text.try_emplace(scene->GetGuid(), text);

			if (added || last->second == text)
				continue;

			m_undo.push_back(Step{ scene->GetGuid(), std::move(last->second), text });
			last->second = std::move(text);

			m_redo.clear();

			if (m_undo.size() > max_steps)
				m_undo.erase(m_undo.begin());
		};
	};

	bool SceneHistory::Undo(const std::vector<Scene*>& scenes, const Rebuild& rebuild)
	{
		return Move(m_undo, m_redo, &Step::before, scenes, rebuild);
	};

	bool SceneHistory::Redo(const std::vector<Scene*>& scenes, const Rebuild& rebuild)
	{
		return Move(m_redo, m_undo, &Step::after, scenes, rebuild);
	};

	bool SceneHistory::Move(
		std::vector<Step>& from,
		std::vector<Step>& to,
		std::string Step::* side,
		const std::vector<Scene*>& scenes,
		const Rebuild& rebuild)
	{
		Record(scenes);

		while (!from.empty())
		{
			Step step = std::move(from.back());
			from.pop_back();

			const auto scene = std::find_if(
				scenes.begin(),
				scenes.end(),
				[&](Scene* open) { return open->GetGuid() == step.scene; });

			if (scene == scenes.end())
				continue;

			Scene* replacement = rebuild(*scene, step.*side);

			if (replacement == nullptr)
			{
				Forget(step.scene);
				return false;
			};

			Engine::DoTasks();

			m_text[replacement->GetGuid()] = SceneSerializer::Serialize(*replacement);
			to.push_back(std::move(step));

			return true;
		};

		return false;
	};

	void SceneHistory::Forget(const Guid& scene)
	{
		m_text.erase(scene);
	};

	Scene* SceneHistory::Replace(Scene* scene, const std::string& text, std::string* error)
	{
		// Work queued against the old objects has to land before they go.
		Engine::DoTasks();

		const std::vector<Scene*>& scenes = Scene::GetScenes();
		const size_t order = std::find(scenes.begin(), scenes.end(), scene) - scenes.begin();

		delete scene;

		Engine::DoTasks();

		Scene* replacement = SceneSerializer::Deserialize(text, error);

		if (replacement)
			Scene::MoveScene(replacement, order);

		Engine::DoTasks();

		return replacement;
	};
};
