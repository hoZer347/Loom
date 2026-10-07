#pragma once

#include "Guid.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>


namespace Loom
{
	struct Scene;

	/**
	* Loom::SceneHistory
	* - Undo and redo over a set of scenes, kept as the text each one serializes
	*   to either side of an edit, so anything the serializer writes can be
	*   undone without each edit having to describe itself
	* - Scenes are told apart by guid, which survives a scene being rebuilt by a
	*   script compile or a Stop
	*/
	struct SceneHistory final
	{
		// Puts a scene into the state the text describes, and hands back the
		// scene that now stands in for it, or null if there is none.
		using Rebuild = std::function<Scene*(Scene* scene, const std::string& text)>;

		// Each step keeps a whole scene either side of it, so the oldest go.
		static constexpr size_t max_steps = 100;

		// Compares each scene with the text it last serialized to, and makes a
		// step of any difference. A scene seen for the first time is only
		// remembered.
		void Record(const std::vector<Scene*>& scenes);

		// Record first, so an edit not compared yet is the one taken back, then
		// step the newest scene that is still among scenes. Steps naming a scene
		// that is not are thrown away on the way. Returns whether a scene was
		// rebuilt.
		bool Undo(const std::vector<Scene*>& scenes, const Rebuild& rebuild);
		bool Redo(const std::vector<Scene*>& scenes, const Rebuild& rebuild);

		bool CanUndo() const { return !m_undo.empty(); };
		bool CanRedo() const { return !m_redo.empty(); };

		// Lets go of a scene that is closing, so one opened later under the same
		// guid starts from what it is then rather than from what this one was.
		void Forget(const Guid& scene);

		// Deletes the scene and builds its replacement from the text, at the same
		// place in Scene::GetScenes. The delete comes first: a reference read
		// while the old objects still answer to their guids would bind to them.
		// Returns null, with error filled in, when the text is not a scene.
		static Scene* Replace(Scene* scene, const std::string& text, std::string* error = nullptr);

	private:
		struct Step
		{
			Guid scene;
			std::string before;
			std::string after;
		};

		bool Move(
			std::vector<Step>& from,
			std::vector<Step>& to,
			std::string Step::* side,
			const std::vector<Scene*>& scenes,
			const Rebuild& rebuild);

		std::vector<Step> m_undo{ };
		std::vector<Step> m_redo{ };

		std::unordered_map<Guid, std::string> m_text{ };
	};
};
