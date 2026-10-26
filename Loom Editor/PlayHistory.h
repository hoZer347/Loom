#pragma once

#include "Guid.h"

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
	*   adding, removing or reordering its components, with each Reference
	*   field elsewhere that pointed at one of them. Putting it back adds,
	*   removes or orders what the edit did and writes only the fields the edit
	*   changed, so whatever the run has done to the rest since is left as it
	*   is
	* - Steps find their objects by guid, so a component a step puts back is
	*   the same one to the steps before it. A step whose object has gone is
	*   thrown away on the way past
	*/
	struct PlayHistory final
	{
		static constexpr size_t max_steps = 100;

		// A step's text, or a field's, either side of an edit.
		struct Sides
		{
			std::string before{ };
			std::string after{ };
		};

		// The field at index of the object with that guid.
		struct Field : Sides
		{
			Guid object{ };
			size_t index = 0;
		};

		// A GameObject's components as an edit to them found them, from
		// ReadComponents, for RecordComponents once the edit is done.
		struct Components
		{
			Guid gameObject{ };
			std::string own{ };
			std::vector<Field> held{ };
		};

		// An edit to object's field at index from before, the text Write gave
		// for it, to what it holds now. One that changed nothing is not a step.
		void RecordField(LoomObject& object, size_t index, const std::string& before);

		// gameObject's components as they are, read before an edit to them.
		static Components ReadComponents(GameObject& gameObject);

		// Components added to, taken off or reordered on the GameObject since
		// it read as before. One that changed nothing is not a step.
		void RecordComponents(Components before);

		// Puts the newest step whose object is still there back, or forward,
		// and returns the GameObject it was on, or null when there was none.
		GameObject* Undo();
		GameObject* Redo();

		bool CanUndo() const { return !m_undo.empty(); };
		bool CanRedo() const { return !m_redo.empty(); };

		void Clear();

	private:
		static constexpr size_t components = (std::numeric_limits<size_t>::max)();

		// A field step, or a component step whose index is components.
		struct Step : Field
		{
			std::vector<Field> held{ };
		};

		void Push(Step step);

		static GameObject* Move(std::vector<Step>& from, std::vector<Step>& to, std::string Sides::* was, std::string Sides::* wanted);

		// Makes gameObject read as to where it read as from, and leaves the rest.
		static void PutComponents(GameObject& gameObject, const std::string& from, const std::string& to);

		std::vector<Step> m_undo{ };
		std::vector<Step> m_redo{ };
	};
};
