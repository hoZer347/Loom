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
	* - A field step is the text of each field one edit changed, on every
	*   object it was made to, either side of it, and putting it back writes
	*   only those fields
	* - A component step is each GameObject's own scene text either side of
	*   adding, removing or reordering its components, with each Reference
	*   field elsewhere that pointed at one of them. Putting it back adds,
	*   removes or orders what the edit did and writes only the fields the edit
	*   changed, so whatever the run has done to the rest since is left as it
	*   is
	* - Steps find their objects by guid, so a component a step puts back is
	*   the same one to the steps before it. A step whose objects have all gone
	*   is thrown away on the way past
	*/
	struct PlayHistory final
	{
		#pragma region Recording

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

		// One edit to each field from its before, the text Write gave for it, to
		// what it holds now, as one step. A field it left as it was is no part
		// of the step, and an edit that changed nothing is not one.
		void RecordFields(std::vector<Field> edits);

		// gameObject's components as they are, read before an edit to them.
		static Components ReadComponents(GameObject& gameObject);

		// Components added to, taken off or reordered on each GameObject since
		// it read as before, as one step. An edit that changed nothing is not
		// one.
		void RecordComponents(std::vector<Components> before);

		#pragma endregion

		#pragma region Stepping

		static constexpr size_t max_steps = 100;

		// Puts the newest step whose objects are still there back, or forward,
		// and returns the GameObjects it was on, or none when there was none.
		std::vector<GameObject*> Undo();
		std::vector<GameObject*> Redo();

		bool CanUndo() const { return !m_undo.empty(); };
		bool CanRedo() const { return !m_redo.empty(); };

		void Clear();

		#pragma endregion

	private:
		#pragma region Stepping

		static constexpr size_t components = (std::numeric_limits<size_t>::max)();

		// One field, or one GameObject's components when index is components.
		struct Part : Field
		{
			std::vector<Field> held{ };
		};

		using Step = std::vector<Part>;

		void Push(Step step);

		static std::vector<GameObject*> Move(std::vector<Step>& from, std::vector<Step>& to, std::string Sides::* was, std::string Sides::* wanted);

		// Makes gameObject read as to where it read as from, and leaves the rest.
		static void PutComponents(GameObject& gameObject, const std::string& from, const std::string& to);

		#pragma endregion

		std::vector<Step> m_undo{ };
		std::vector<Step> m_redo{ };
	};
};
