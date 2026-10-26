#pragma once

#include "Transform.h"

#include "glm/glm.hpp"

#include <cstdint>
#include <vector>


namespace Loom
{
	struct GameObject;

	/**
	* Loom::TransformHistory
	* - Undo and redo of the Transform edits made in the Inspector while the
	*   game runs, which SceneHistory never sees, since nothing is watched
	*   while a run changes the scenes every frame
	* - A step is one field's value either side of one edit, and putting it
	*   back writes only that field, so whatever the run has done to the rest
	*   since is left as it is
	* - A step whose GameObject has gone is thrown away on the way past
	*/
	struct TransformHistory final
	{
		static constexpr size_t max_steps = 100;

		using Field = Serial<glm::vec3> Transform::*;

		// An edit to field of gameObject's Transform from before to what it
		// holds now. One that changed nothing is not a step.
		void Record(GameObject& gameObject, Field field, const glm::vec3& before);

		// Puts the newest step whose GameObject is still there back, or
		// forward, and returns that GameObject, or null when there was none.
		GameObject* Undo();
		GameObject* Redo();

		bool CanUndo() const { return !m_undo.empty(); };
		bool CanRedo() const { return !m_redo.empty(); };

		void Clear();

	private:
		struct Step
		{
			GameObject* object = nullptr;
			uint64_t id = 0;
			Field field = nullptr;
			glm::vec3 before{ };
			glm::vec3 after{ };
		};

		static GameObject* Move(std::vector<Step>& from, std::vector<Step>& to, glm::vec3 Step::* value);

		std::vector<Step> m_undo{ };
		std::vector<Step> m_redo{ };
	};
};
