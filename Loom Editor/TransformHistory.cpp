#include "TransformHistory.h"

#include "GameObject.h"


namespace Loom
{
	void TransformHistory::Record(GameObject& gameObject, Field field, const glm::vec3& before)
	{
		const glm::vec3 after = gameObject.transform.*field;

		if (after == before)
			return;

		m_redo.clear();

		if (m_undo.size() == max_steps)
			m_undo.erase(m_undo.begin());

		m_undo.push_back(Step{ &gameObject, gameObject.m_ID, field, before, after });
	};

	GameObject* TransformHistory::Undo()
	{
		return Move(m_undo, m_redo, &Step::before);
	};

	GameObject* TransformHistory::Redo()
	{
		return Move(m_redo, m_undo, &Step::after);
	};

	void TransformHistory::Clear()
	{
		m_undo.clear();
		m_redo.clear();
	};

	GameObject* TransformHistory::Move(std::vector<Step>& from, std::vector<Step>& to, glm::vec3 Step::* value)
	{
		while (!from.empty())
		{
			const Step step = from.back();

			from.pop_back();

			// The address alone could be a new object made where an old one was.
			if (LoomObject::GetByID(step.id) != step.object)
				continue;

			step.object->transform.*step.field = step.*value;
			to.push_back(step);

			return step.object;
		};

		return nullptr;
	};
};
