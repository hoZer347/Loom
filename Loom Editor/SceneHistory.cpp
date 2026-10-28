#include "SceneHistory.h"

#include "Engine.h"
#include "Scene.h"
#include "SceneSerializer.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <utility>


namespace Loom
{
	#pragma region Recording

	void SceneHistory::Collect(const std::vector<Scene*>& scenes)
	{
		// AddChild and Destroy defer; what is kept has to show where they land.
		Engine::DoTasks();

		Take(scenes, true);
	};

	void SceneHistory::Record(const std::vector<Scene*>& scenes)
	{
		Collect(scenes);

		Step step{ };

		for (Change& change : m_pending)
			if (change.before != change.after)
			{
				if (std::find(step.scenes.begin(), step.scenes.end(), change.scene) == step.scenes.end())
					step.scenes.push_back(change.scene);

				step.changes.push_back(std::move(change));
			};

		m_pending.clear();
		m_pendingAt.clear();

		if (step.changes.empty())
			return;

		step.id = ++m_lastStep;
		m_undo.push_back(std::move(step));
		m_redo.clear();

		if (m_undo.size() > max_steps)
		{
			const Step& front = m_undo.front();

			// A scene saved before the step let go can no longer be undone back to it.
			for (const Guid& scene : front.scenes)
				if (unsigned long long& saved = m_saved[scene]; saved < front.id)
					saved = never_saved;

			for (auto& [scene, saved] : m_saved)
				if (saved == front.id)
					saved = 0;

			m_undo.erase(m_undo.begin());
		};
	};

	void SceneHistory::Take(const std::vector<Scene*>& scenes, bool record)
	{
		m_scenes = scenes;

		LoomObject::Journal journal = LoomObject::TakeJournal();

		// Whatever was kept by address while nothing was watched may be gone.
		if (LoomObject::WatchCount() != m_watchCount)
		{
			m_watchCount = LoomObject::WatchCount();
			journal = { };

			while (!m_trackers.empty())
				Drop(*m_trackers.back());
		};

		const std::unordered_set<Scene*> open(scenes.begin(), scenes.end());

		// A scene no longer listed may be gone, so only listed ones are asked.
		for (size_t i = m_trackers.size(); i-- > 0;)
			if (!open.contains(m_trackers[i]->scene) || m_trackers[i]->scene->m_ID != m_trackers[i]->id)
				Drop(*m_trackers[i]);

		// First, since what took a destroyed object's address is in touched.
		for (const LoomObject* gone : journal.destroyed)
			Remove(gone, record);

		// Each with whether what it holds changed, and where it sits.
		std::unordered_map<GameObject*, std::pair<bool, bool>> changed{ };

		for (const auto& [object, own, placed] : journal.touched)
		{
			if (Scene* scene = dynamic_cast<Scene*>(object))
			{
				for (const std::unique_ptr<Tracker>& tracker : m_trackers)
					if (tracker->scene == scene)
					{
						std::string header = SceneSerializer::SerializeHeader(*scene);

						if (header == tracker->header)
							continue;

						Note(*tracker, tracker->guid, State{ tracker->header }, State{ header }, record);
						tracker->header = std::move(header);
					};

				continue;
			};

			GameObject* gameObject = dynamic_cast<GameObject*>(object);
			bool holds = own;

			if (ComponentBase* component = gameObject ? nullptr : dynamic_cast<ComponentBase*>(object))
			{
				gameObject = component->GetGameObject();
				holds = true;
			};

			if (gameObject == nullptr)
				continue;

			auto& [was_held, was_placed] = changed[gameObject];

			was_held = was_held || holds;
			was_placed = was_placed || placed;
		};

		// Placements name it by guid.
		std::vector<GameObject*> renamed{ };

		for (const auto& [gameObject, what] : changed)
			if (const auto found = m_entries.find(gameObject); found != m_entries.end() && found->second.guid != gameObject->GetGuid())
				renamed.push_back(gameObject);

		for (GameObject* gameObject : renamed)
		{
			for (GameObject* child : gameObject->GetChildren())
				changed[child].second = true;

			const GameObject* parent = gameObject->GetParent();
			const size_t at = gameObject->GetSiblingIndex();

			if (parent && at > 0 && at < parent->GetChildren().size())
				changed[parent->GetChildren()[at - 1]].second = true;
		};

		for (const auto& [gameObject, what] : changed)
			Update(*gameObject, what.first, what.second, record);

		// Last, so nothing above is taken for an edit to a scene only now seen.
		std::vector<Tracker*> fresh{ };

		for (Scene* scene : scenes)
		{
			if (m_roots.contains(&scene->GetRoot()))
				continue;

			Tracker& tracker = *m_trackers.emplace_back(std::make_unique<Tracker>());

			tracker.scene = scene;
			tracker.root = &scene->GetRoot();
			tracker.id = scene->m_ID;
			tracker.guid = scene->GetGuid();
			tracker.header = SceneSerializer::SerializeHeader(*scene);

			m_roots[tracker.root] = &tracker;
			Adopt(scene->GetRoot(), tracker, false);
			fresh.push_back(&tracker);
		};

		bool copied = false;

		for (Tracker* tracker : fresh)
		{
			const auto original = std::find_if(
				m_trackers.begin(),
				m_trackers.end(),
				[&](const std::unique_ptr<Tracker>& other)
				{
					return other->guid == tracker->guid && std::find(fresh.begin(), fresh.end(), other.get()) == fresh.end();
				});

			if (original == m_trackers.end())
				continue;

			States states{ };

			states.reserve((*original)->objects.size() + tracker->objects.size() + 1);
			states.emplace_back(tracker->guid, State{ (*original)->header });

			for (const auto& [guid, object] : (*original)->objects)
				states.emplace_back(guid, m_entries.at(object).state);

			for (const auto& [guid, object] : tracker->objects)
				if (!(*original)->objects.contains(guid))
					states.emplace_back(guid, std::nullopt);

			Apply({ { tracker, WantedOf(states) } });
			copied = true;
		};

		for (const auto& [source, changes] : m_copyChanges)
			for (const std::unique_ptr<Tracker>& tracker : m_trackers)
				if (tracker.get() != source && tracker->guid == source->guid)
				{
					Apply({ { tracker.get(), WantedOf(changes) } });
					copied = true;
				};

		m_copyChanges.clear();

		if (!copied)
			return;

		Engine::DoTasks();
		Take(scenes, false);
	};

	void SceneHistory::Drop(Tracker& tracker)
	{
		for (const auto& [guid, object] : tracker.objects)
		{
			const auto entry = m_entries.find(object);

			if (entry != m_entries.end() && entry->second.tracker == &tracker)
			{
				m_changed.erase(object);
				m_entries.erase(entry);
			};
		};

		m_roots.erase(tracker.root);
		m_copyChanges.erase(&tracker);

		std::erase_if(m_trackers, [&](const std::unique_ptr<Tracker>& kept) { return kept.get() == &tracker; });
	};

	void SceneHistory::Note(
		Tracker& tracker,
		const Guid& object,
		const std::optional<State>& before,
		const std::optional<State>& after,
		bool record)
	{
		if (!record)
			return;

		m_copyChanges[&tracker].emplace_back(object, after);

		const auto [at, first] = m_pendingAt[tracker.guid].try_emplace(object, m_pending.size());

		if (first)
			m_pending.push_back(Change{ tracker.guid, object, before, after });
		else m_pending[at->second].after = after;
	};

	#pragma endregion

	#pragma region Steps

	bool SceneHistory::Undo(const std::vector<Scene*>& scenes)
	{
		return Move(m_undo, m_redo, &Change::before, scenes);
	};

	bool SceneHistory::Redo(const std::vector<Scene*>& scenes)
	{
		return Move(m_redo, m_undo, &Change::after, scenes);
	};

	SceneHistory::Wanted SceneHistory::WantedOf(const States& states)
	{
		Wanted wanted{ };

		wanted.reserve(states.size());

		for (const auto& [guid, state] : states)
			wanted.emplace_back(guid, &state);

		return wanted;
	};

	void SceneHistory::Apply(const std::unordered_map<Tracker*, Wanted>& wanted)
	{
		Engine::DoTasks();

		// By guid, ahead of what each tracker last saw.
		std::unordered_map<Tracker*, std::unordered_map<Guid, LoomObject*>> made{ };

		const auto find =
			[&](Tracker& tracker, const Guid& guid) -> LoomObject*
			{
				const std::unordered_map<Guid, LoomObject*>& ours = made[&tracker];

				if (const auto found = ours.find(guid); found != ours.end())
					return found->second;

				if (const auto found = tracker.objects.find(guid); found != tracker.objects.end())
					return found->second;

				if (const auto found = tracker.components.find(guid); found != tracker.components.end())
					return found->second;

				return LoomObject::GetByGuid(guid);
			};

		// Moved rather than recreated, so it keeps its components and whatever points at it.
		std::vector<GameObject*> removed{ };
		std::unordered_map<Guid, GameObject*> movable{ };

		for (const auto& [tracker, objects] : wanted)
			for (const auto& [guid, state] : objects)
				if (!state->has_value())
					if (const auto found = tracker->objects.find(guid); found != tracker->objects.end())
					{
						removed.push_back(found->second);
						movable[guid] = found->second;
					};

		std::unordered_set<GameObject*> moved{ };

		for (const auto& [tracker, objects] : wanted)
			for (const auto& [guid, state] : objects)
			{
				if (!state->has_value() || guid == tracker->guid || tracker->objects.contains(guid))
					continue;

				GameObject* gameObject = nullptr;

				if (const auto found = movable.find(guid); found != movable.end())
				{
					gameObject = found->second;
					moved.insert(gameObject);
					movable.erase(found);
				}
				else
				{
					gameObject = tracker->scene->GetRoot().AddChild();
					gameObject->SetGuid(guid);
				};

				made[tracker][guid] = gameObject;
			};

		Engine::DoTasks();

		std::unordered_map<Tracker*, SceneSerializer::PendingReferences> pending{ };

		// The objects to be placed, with where.
		std::unordered_map<GameObject*, std::pair<Tracker*, const State*>> unplaced{ };
		std::vector<GameObject*> order{ };

		for (const auto& [tracker, objects] : wanted)
			for (const auto& [guid, state] : objects)
			{
				if (!state->has_value())
					continue;

				if (guid == tracker->guid)
				{
					SceneSerializer::ReadHeader(*tracker->scene, (*state)->own, pending[tracker]);

					continue;
				};

				GameObject* gameObject = dynamic_cast<GameObject*>(find(*tracker, guid));

				if (gameObject == nullptr)
					continue;

				SceneSerializer::ReadOwn(*gameObject, (*state)->own, pending[tracker]);

				for (ComponentBase* component : gameObject->GetComponents())
					made[tracker][component->GetGuid()] = component;

				if ((*state)->parent.IsValid())
				{
					unplaced[gameObject] = { tracker, &**state };
					order.push_back(gameObject);
				};
			};

		for (const auto& [tracker, references] : pending)
			for (const auto& [field, guid] : references)
			{
				field->SetReference(find(*tracker, guid));

				if (field->owner)
					field->owner->OnFieldChanged(*field);
			};

		// Off to the root first, so no move waits on one under it.
		for (GameObject* gameObject : order)
		{
			const auto [tracker, state] = unplaced.at(gameObject);

			if (gameObject->GetParent() != find(*tracker, state->parent))
				gameObject->SetParent(&tracker->scene->GetRoot());
		};

		Engine::DoTasks();

		// Each before the sibling it precedes, so that one is placed first.
		for (GameObject* first : order)
		{
			std::vector<std::tuple<GameObject*, GameObject*, GameObject*>> chain{ };

			for (GameObject* gameObject = first; gameObject != nullptr;)
			{
				const auto found = unplaced.find(gameObject);

				if (found == unplaced.end())
					break;

				const auto [tracker, state] = found->second;
				GameObject* parent = dynamic_cast<GameObject*>(find(*tracker, state->parent));
				GameObject* next = state->next.IsValid() ? dynamic_cast<GameObject*>(find(*tracker, state->next)) : nullptr;

				unplaced.erase(found);
				chain.emplace_back(gameObject, parent, next);
				gameObject = next;
			};

			for (auto link = chain.rbegin(); link != chain.rend(); link++)
			{
				const auto [gameObject, parent, next] = *link;

				if (parent == nullptr)
					continue;

				const std::vector<GameObject*>& siblings = parent->GetChildren();
				const size_t at = gameObject->GetSiblingIndex();

				if (gameObject->GetParent() == parent && at < siblings.size() && (at + 1 < siblings.size() ? siblings[at + 1] : nullptr) == next)
					continue;

				gameObject->SetParent(parent, next);
				Engine::DoTasks();
			};
		};

		// One under another that goes goes with it.
		std::unordered_set<GameObject*> going{ };

		for (GameObject* gameObject : removed)
			if (!moved.contains(gameObject))
				going.insert(gameObject);

		for (GameObject* gameObject : going)
		{
			bool inside = false;

			for (const GameObject* above = gameObject->GetParent(); above && !inside; above = above->GetParent())
				inside = going.contains(const_cast<GameObject*>(above));

			if (!inside)
				gameObject->Destroy();
		};

		Engine::DoTasks();
	};

	bool SceneHistory::Move(
		std::vector<Step>& from,
		std::vector<Step>& to,
		std::optional<State> Change::* side,
		const std::vector<Scene*>& scenes)
	{
		Record(scenes);

		// Each scene's copies, in the order they were opened.
		std::unordered_map<Guid, std::vector<Tracker*>> copies{ };
		size_t most = 0;

		for (const std::unique_ptr<Tracker>& tracker : m_trackers)
		{
			std::vector<Tracker*>& of_scene = copies[tracker->guid];

			of_scene.push_back(tracker.get());
			most = std::max(most, of_scene.size());
		};

		while (!from.empty())
		{
			Step step = std::move(from.back());
			from.pop_back();

			bool applied = false;

			// One copy of every scene at a time, so a move between scenes stays within one copy each.
			for (size_t copy = 0; copy < most; copy++)
			{
				std::unordered_map<Tracker*, Wanted> wanted{ };

				for (const Change& change : step.changes)
				{
					const auto found = copies.find(change.scene);

					if (found != copies.end() && copy < found->second.size())
						wanted[found->second[copy]].emplace_back(change.object, &(change.*side));
				};

				if (wanted.empty())
					continue;

				Apply(wanted);
				applied = true;
			};

			if (!applied)
				continue;

			Engine::DoTasks();
			Take(scenes, false);

			to.push_back(std::move(step));

			return true;
		};

		return false;
	};

	unsigned long long SceneHistory::NewestOf(const Guid& scene) const
	{
		for (auto step = m_undo.rbegin(); step != m_undo.rend(); step++)
			if (std::find(step->scenes.begin(), step->scenes.end(), scene) != step->scenes.end())
				return step->id;

		return 0;
	};

	#pragma endregion

	#pragma region Saving

	void SceneHistory::Saved(const Scene& scene)
	{
		m_saved[scene.GetGuid()] = NewestOf(scene.GetGuid());
	};

	bool SceneHistory::Unsaved(const Scene& scene) const
	{
		const Guid guid = scene.GetGuid();

		if (const auto pending = m_pendingAt.find(guid); pending != m_pendingAt.end())
			for (const auto& [object, at] : pending->second)
				if (m_pending[at].before != m_pending[at].after)
					return true;

		const auto saved = m_saved.find(guid);

		return NewestOf(guid) != (saved == m_saved.end() ? 0 : saved->second);
	};

	#pragma endregion

	#pragma region Queries

	std::unordered_set<GameObject*> SceneHistory::TakeChanged()
	{
		return std::exchange(m_changed, { });
	};

	GameObject* SceneHistory::Find(const Scene& scene, const Guid& guid) const
	{
		for (const std::unique_ptr<Tracker>& tracker : m_trackers)
			if (tracker->scene == &scene && tracker->id == scene.m_ID)
			{
				const auto found = tracker->objects.find(guid);

				return found == tracker->objects.end() ? nullptr : found->second;
			};

		return nullptr;
	};

	Scene* SceneHistory::SceneOf(const GameObject& gameObject) const
	{
		const Tracker* tracker = TrackerOf(gameObject);

		return tracker ? tracker->scene : nullptr;
	};

	SceneHistory::Tracker* SceneHistory::TrackerOf(const GameObject& gameObject) const
	{
		const GameObject* top = &gameObject;

		while (top->GetParent())
			top = top->GetParent();

		const auto found = m_roots.find(top);

		return found == m_roots.end() ? nullptr : found->second;
	};

	#pragma endregion

	#pragma region Rewriting

	void SceneHistory::Rewrite(GameObject& root, const std::string& text)
	{
		Tracker* tracker = TrackerOf(root);
		const auto entry = m_entries.find(&root);

		if (tracker == nullptr || entry == m_entries.end())
			return;

		struct Open
		{
			int depth;
			size_t at;
			size_t last;
		};

		constexpr size_t none = (size_t)-1;
		const size_t keyword = strlen(SceneSerializer::gameobject_keyword);

		States states{ };
		std::vector<Open> open{ };
		std::unordered_set<Guid> named{ };
		std::istringstream lines(text);

		for (std::string line; std::getline(lines, line);)
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			size_t start = 0;
			const int depth = SceneSerializer::DepthOf(line, start);

			if (start >= line.size())
				continue;

			const std::string content = line.substr(start);

			if (content.rfind(SceneSerializer::gameobject_keyword, 0) != 0)
			{
				if (open.empty())
					return;

				states[open.back().at].second->own += std::string(depth - open.back().depth, '\t') + content + '\n';

				continue;
			};

			std::string name;
			Guid guid;

			if (!SceneSerializer::SplitNameAndGuid(content.substr(keyword), name, guid))
				return;

			while (!open.empty() && open.back().depth >= depth)
				open.pop_back();

			State state{ content + '\n' };

			if (open.empty())
			{
				// One object, which keeps the place root has.
				if (!states.empty())
					return;

				state.parent = entry->second.state.parent;
				state.next = entry->second.state.next;
			}
			else
			{
				state.parent = states[open.back().at].first;

				if (open.back().last != none)
					states[open.back().last].second->next = guid;

				open.back().last = states.size();
			};

			open.push_back({ depth, states.size(), none });
			states.emplace_back(guid, std::move(state));
			named.insert(guid);
		};

		if (states.empty())
			return;

		std::vector<const GameObject*> below{ &root };

		while (!below.empty())
		{
			const GameObject* gameObject = below.back();
			below.pop_back();

			if (!named.contains(gameObject->GetGuid()))
				states.emplace_back(gameObject->GetGuid(), std::nullopt);

			below.insert(below.end(), gameObject->GetChildren().begin(), gameObject->GetChildren().end());
		};

		Apply({ { tracker, WantedOf(states) } });

		Engine::DoTasks();
		Take(m_scenes, true);
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

	#pragma endregion

	#pragma region Entries

	void SceneHistory::Place(State& state, const GameObject& gameObject)
	{
		state.parent = { };
		state.next = { };

		const GameObject* parent = gameObject.GetParent();

		if (parent == nullptr)
			return;

		const std::vector<GameObject*>& siblings = parent->GetChildren();
		const size_t at = gameObject.GetSiblingIndex();

		state.parent = parent->GetGuid();

		if (at + 1 < siblings.size())
			state.next = siblings[at + 1]->GetGuid();
	};

	void SceneHistory::Rearranged(const Tracker& tracker, const Guid& parent)
	{
		if (const auto found = tracker.objects.find(parent); found != tracker.objects.end())
			m_changed.insert(found->second);
	};

	void SceneHistory::Update(GameObject& gameObject, bool own, bool placed, bool record)
	{
		Tracker* tracker = TrackerOf(gameObject);
		const auto found = m_entries.find(&gameObject);

		if (tracker == nullptr)
		{
			if (found == m_entries.end())
				return;

			std::vector<GameObject*> below{ &gameObject };

			while (!below.empty())
			{
				GameObject* gone = below.back();
				below.pop_back();

				Remove(gone, record);
				below.insert(below.end(), gone->GetChildren().begin(), gone->GetChildren().end());
			};

			return;
		};

		if (found == m_entries.end() || found->second.tracker != tracker || found->second.guid != gameObject.GetGuid())
		{
			Adopt(gameObject, *tracker, record);

			return;
		};

		Entry& entry = found->second;
		State state = entry.state;

		if (own)
			state.own = SceneSerializer::SerializeOwn(gameObject);

		if (placed)
			Place(state, gameObject);

		if (own)
		{
			std::vector<Guid> components{ };

			for (ComponentBase* component : gameObject.GetComponents())
			{
				components.push_back(component->GetGuid());
				tracker->components[component->GetGuid()] = component;
			};

			for (const Guid& had : entry.components)
				if (std::find(components.begin(), components.end(), had) == components.end())
					tracker->components.erase(had);

			entry.components = std::move(components);
		};

		Set(entry, std::move(state), record);
	};

	void SceneHistory::Adopt(GameObject& top, Tracker& tracker, bool record)
	{
		std::vector<GameObject*> below{ &top };

		while (!below.empty())
		{
			GameObject* gameObject = below.back();
			below.pop_back();

			const auto found = m_entries.find(gameObject);

			if (found == m_entries.end() || found->second.tracker != &tracker || found->second.guid != gameObject->GetGuid())
			{
				Remove(gameObject, record);
				Add(*gameObject, tracker, record);
			};

			below.insert(below.end(), gameObject->GetChildren().begin(), gameObject->GetChildren().end());
		};
	};

	void SceneHistory::Add(GameObject& gameObject, Tracker& tracker, bool record)
	{
		// Steps find objects by guid, so a scene may not hold two under one.
		const auto taken = tracker.objects.find(gameObject.GetGuid());

		if (taken != tracker.objects.end() && taken->second != &gameObject)
			gameObject.SetGuid(Guid::New());

		Entry& entry = m_entries[&gameObject];

		entry = { &tracker, &gameObject, gameObject.GetGuid(), State{ SceneSerializer::SerializeOwn(gameObject) } };
		Place(entry.state, gameObject);

		for (ComponentBase* component : gameObject.GetComponents())
		{
			entry.components.push_back(component->GetGuid());
			tracker.components[component->GetGuid()] = component;
		};

		tracker.objects[entry.guid] = &gameObject;
		m_changed.insert(&gameObject);
		Rearranged(tracker, entry.state.parent);

		Note(tracker, entry.guid, std::nullopt, entry.state, record);
	};

	void SceneHistory::Remove(const LoomObject* object, bool record)
	{
		const auto found = m_entries.find(object);

		if (found == m_entries.end())
			return;

		Entry& entry = found->second;
		Tracker& tracker = *entry.tracker;

		Note(tracker, entry.guid, entry.state, std::nullopt, record);
		Rearranged(tracker, entry.state.parent);

		const auto at = tracker.objects.find(entry.guid);

		if (at != tracker.objects.end() && at->second == entry.object)
			tracker.objects.erase(at);

		for (const Guid& component : entry.components)
			tracker.components.erase(component);

		m_changed.erase(entry.object);
		m_entries.erase(found);
	};

	void SceneHistory::Set(Entry& entry, State state, bool record)
	{
		if (state == entry.state)
			return;

		Note(*entry.tracker, entry.guid, entry.state, state, record);

		if (state.parent != entry.state.parent || state.next != entry.state.next)
		{
			Rearranged(*entry.tracker, entry.state.parent);
			Rearranged(*entry.tracker, state.parent);
		};

		entry.state = std::move(state);
		m_changed.insert(entry.object);
	};

	#pragma endregion
};
