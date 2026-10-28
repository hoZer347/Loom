#pragma once

#include "Guid.h"

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


namespace Loom
{
	struct GameObject;
	struct LoomObject;
	struct Scene;

	/**
	* Loom::SceneHistory
	* - Undo and redo over a set of scenes, kept per GameObject as the text it
	*   serializes to without its children, and where it sits, either side of
	*   an edit, so anything the serializer writes can be undone without each
	*   edit having to describe itself
	* - Only what LoomObject's journal names is looked at again, and a step is
	*   put back in place, so neither costs more in a larger scene. Needs the
	*   journal watched
	* - Scenes and objects are told apart by guid, which survives a scene
	*   being rebuilt by a script compile or a Stop
	*/
	struct SceneHistory final
	{
		#pragma region Recording

		static constexpr size_t max_steps = 100;

		// Brings what is kept of the scenes up to date with what changed. A
		// scene seen for the first time is only remembered, and a scene open
		// twice is one scene edited in several places: a change to one copy
		// is made to the others, and a copy opened later is made the same as
		// the one already open.
		void Collect(const std::vector<Scene*>& scenes);

		// Collect, then one step of every difference since the last step, so
		// an edit that spans scenes, such as moving an object from one to
		// another, goes back as one.
		void Record(const std::vector<Scene*>& scenes);

		#pragma endregion

		#pragma region Steps

		// Record first, so an edit not recorded yet is the one taken back,
		// then put the newest step with a scene still among scenes back in
		// place. Steps with none are thrown away on the way. Returns whether
		// one was put back.
		bool Undo(const std::vector<Scene*>& scenes);
		bool Redo(const std::vector<Scene*>& scenes);

		bool CanUndo() const { return !m_undo.empty(); };
		bool CanRedo() const { return !m_redo.empty(); };

		#pragma endregion

		#pragma region Saving

		// Takes the scene as it stands, as of the last Record, as what is on
		// disk. A step back past it, or a new one on top, leaves it unsaved.
		void Saved(const Scene& scene);
		bool Unsaved(const Scene& scene) const;

		#pragma endregion

		#pragma region Queries

		// The GameObjects seen changing since the last call, by an edit or a
		// step put back: those written or moved, those whose children came,
		// went or moved, and every one in a scene seen for the first time.
		std::unordered_set<GameObject*> TakeChanged();

		// As of the last Collect: the GameObject under guid in scene, and the
		// scene gameObject is in. Null when there is none.
		GameObject* Find(const Scene& scene, const Guid& guid) const;
		Scene* SceneOf(const GameObject& gameObject) const;

		#pragma endregion

		#pragma region Rewriting

		// Makes root and everything under it what the text says, as
		// SceneSerializer writes one GameObject, in place, and collects that
		// as an edit like any other. Objects keep their guids, so the text
		// says which are kept, which are made and which go.
		void Rewrite(GameObject& root, const std::string& text);

		// Deletes the scene and builds its replacement from the text, at the same
		// place in Scene::GetScenes. The delete comes first: a reference read
		// while the old objects still answer to their guids would bind to them.
		// Returns null, with error filled in, when the text is not a scene.
		static Scene* Replace(Scene* scene, const std::string& text, std::string* error = nullptr);

		#pragma endregion

	private:
		#pragma region Types

		// A GameObject as it is kept: its own text, and its parent and the
		// sibling after it, by guid. A scene's header is kept the same way
		// under the scene's guid, with no parent.
		struct State
		{
			std::string own;
			Guid parent{ };
			Guid next{ };

			bool operator==(const State&) const = default;
		};

		struct Change
		{
			Guid scene;
			Guid object;
			std::optional<State> before;
			std::optional<State> after;
		};

		struct Step
		{
			unsigned long long id = 0;
			std::vector<Guid> scenes{ };
			std::vector<Change> changes{ };
		};

		// One open scene, which a scene's copies each have their own of.
		struct Tracker
		{
			Scene* scene = nullptr;
			const GameObject* root = nullptr;
			unsigned long long id = 0;
			Guid guid{ };
			std::string header{ };
			std::unordered_map<Guid, GameObject*> objects{ };
			std::unordered_map<Guid, LoomObject*> components{ };
		};

		struct Entry
		{
			Tracker* tracker = nullptr;
			GameObject* object = nullptr;
			Guid guid{ };
			State state{ };
			std::vector<Guid> components{ };
		};

		// What a tracker's objects are to be, by guid: absent for one to go.
		using Wanted = std::vector<std::pair<Guid, const std::optional<State>*>>;
		using States = std::vector<std::pair<Guid, std::optional<State>>>;

		#pragma endregion

		#pragma region Recording

		void Take(const std::vector<Scene*>& scenes, bool record);
		void Drop(Tracker& tracker);
		void Note(Tracker& tracker, const Guid& object, const std::optional<State>& before, const std::optional<State>& after, bool record);

		#pragma endregion

		#pragma region Steps

		static Wanted WantedOf(const States& states);

		void Apply(const std::unordered_map<Tracker*, Wanted>& wanted);

		bool Move(std::vector<Step>& from, std::vector<Step>& to, std::optional<State> Change::* side, const std::vector<Scene*>& scenes);

		// The newest step to undo that changed the scene, 0 with none.
		unsigned long long NewestOf(const Guid& scene) const;

		#pragma endregion

		#pragma region Queries

		Tracker* TrackerOf(const GameObject& gameObject) const;

		#pragma endregion

		#pragma region Entries

		// Fills in where gameObject sits now.
		static void Place(State& state, const GameObject& gameObject);

		// Counts parent, by guid, among the changed for its children.
		void Rearranged(const Tracker& tracker, const Guid& parent);

		// Brings gameObject's entry up to date, its own text when own is set
		// and where it sits when placed is. One no tracker has seen, or that
		// came from another, has its subtree adopted with it, and one in no
		// tracker's scene is let go.
		void Update(GameObject& gameObject, bool own, bool placed, bool record);
		void Adopt(GameObject& top, Tracker& tracker, bool record);
		void Add(GameObject& gameObject, Tracker& tracker, bool record);
		void Remove(const LoomObject* object, bool record);
		void Set(Entry& entry, State state, bool record);

		#pragma endregion

		std::vector<Step> m_undo{ };
		std::vector<Step> m_redo{ };
		unsigned long long m_lastStep = 0;

		static constexpr unsigned long long never_saved = std::numeric_limits<unsigned long long>::max();

		// Each scene's NewestOf when it was saved.
		std::unordered_map<Guid, unsigned long long> m_saved{ };

		// The differences since the last step, by scene and object, each
		// from what it was before the first of them.
		std::vector<Change> m_pending{ };
		std::unordered_map<Guid, std::unordered_map<Guid, size_t>> m_pendingAt{ };

		// The last Collect's changes per tracker, as they came out, for the
		// tracker's copies to be made the same.
		std::unordered_map<Tracker*, States> m_copyChanges{ };

		std::vector<std::unique_ptr<Tracker>> m_trackers{ };
		std::unordered_map<const GameObject*, Tracker*> m_roots{ };
		std::unordered_map<const LoomObject*, Entry> m_entries{ };
		std::unordered_set<GameObject*> m_changed{ };
		std::vector<Scene*> m_scenes{ };
		unsigned long long m_watchCount = 0;
	};
};
