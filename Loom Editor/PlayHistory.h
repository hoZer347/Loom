#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>


namespace Loom
{
	struct GameObject;
	struct LoomObject;

	/**
	* Loom::PlayHistory
	* - Undo and redo of the edits made in the Inspector while the game runs,
	*   which SceneHistory never sees, since nothing is watched while a run
	*   changes the scenes every frame
	* - A field step is one field's text either side of one edit, and putting
	*   it back writes only that field
	* - A component step is a GameObject's own scene text either side of
	*   adding or removing components. Putting it back adds or removes what
	*   the edit did and writes only the fields the edit changed, such as a
	*   reference to a removed component, so whatever the run has done to the
	*   rest since is left as it is
	* - A step whose object has gone is thrown away on the way past
	*/
	struct PlayHistory final
	{
		static constexpr size_t max_steps = 100;

		// An edit to object's field at index from before, the text Write gave
		// for it, to what it holds now. One that changed nothing is not a step.
		void RecordField(LoomObject& object, size_t index, const std::string& before);

		// Components added to or taken off gameObject since it read as before,
		// the text SerializeOwn gave for it. One that changed nothing is not a
		// step.
		void RecordComponents(GameObject& gameObject, const std::string& before);

		// Puts the newest step whose object is still there back, or forward,
		// and returns the GameObject it was on, or null when there was none.
		GameObject* Undo();
		GameObject* Redo();

		bool CanUndo() const { return !m_undo.empty(); };
		bool CanRedo() const { return !m_redo.empty(); };

		void Clear();

	private:
		static constexpr size_t components = (std::numeric_limits<size_t>::max)();

		struct Step
		{
			LoomObject* object = nullptr;
			uint64_t id = 0;

			// The field's index, or components for a component step.
			size_t index = components;
			std::string before{ };
			std::string after{ };
		};

		void Push(Step step);

		static GameObject* Move(std::vector<Step>& from, std::vector<Step>& to, std::string Step::* was, std::string Step::* wanted);

		// Makes gameObject read as to where it read as from, and leaves the rest.
		static void PutComponents(GameObject& gameObject, const std::string& from, const std::string& to);

		std::vector<Step> m_undo{ };
		std::vector<Step> m_redo{ };
	};
};
