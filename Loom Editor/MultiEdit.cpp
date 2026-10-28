#include "MultiEdit.h"

#include "Component.h"
#include "ComponentRegistry.h"
#include "GameObject.h"
#include "LoomObject.h"
#include "SerializedField.h"

#include <algorithm>
#include <string>
#include <typeinfo>


namespace Loom
{
	namespace
	{
		bool SameKind(const ComponentBase& a, const ComponentBase& b)
		{
			return typeid(a) == typeid(b) && ComponentRegistry::NameOf(a) == ComponentRegistry::NameOf(b);
		};

		void AddRows(GameObject& node, const std::function<bool(const GameObject&)>& open, std::vector<GameObject*>& rows)
		{
			for (GameObject* child : node.GetChildren())
			{
				rows.push_back(child);

				if (open(*child))
					AddRows(*child, open, rows);
			};
		};
	};

	#pragma region Selection

	std::vector<GameObject*> MultiEdit::Rows(GameObject& root, const std::function<bool(const GameObject&)>& open)
	{
		std::vector<GameObject*> rows{ };

		AddRows(root, open, rows);

		return rows;
	};

	std::vector<GameObject*> MultiEdit::Range(const std::vector<GameObject*>& rows, const GameObject* a, const GameObject* b)
	{
		const auto first = std::find(rows.begin(), rows.end(), a);
		const auto last = std::find(rows.begin(), rows.end(), b);

		if (first == rows.end() || last == rows.end())
			return { };

		return first < last
			? std::vector<GameObject*>(first, last + 1)
			: std::vector<GameObject*>(last, first + 1);
	};

	std::vector<GameObject*> MultiEdit::Tops(const std::vector<GameObject*>& objects)
	{
		std::vector<GameObject*> tops{ };

		for (GameObject* object : objects)
		{
			bool under = false;

			for (const GameObject* above = object->GetParent(); above && !under; above = above->GetParent())
				under = std::find(objects.begin(), objects.end(), above) != objects.end();

			if (!under)
				tops.push_back(object);
		};

		return tops;
	};

	#pragma endregion

	#pragma region Components

	std::vector<std::vector<ComponentBase*>> MultiEdit::Common(const std::vector<GameObject*>& objects)
	{
		std::vector<std::vector<ComponentBase*>> common{ };

		if (objects.empty())
			return common;

		const std::vector<ComponentBase*>& own = objects.front()->GetComponents();

		for (size_t i = 0; i < own.size(); i++)
		{
			const size_t along = (size_t)std::count_if(
				own.begin(),
				own.begin() + i,
				[&](const ComponentBase* earlier) { return SameKind(*earlier, *own[i]); });

			std::vector<ComponentBase*> matches{ own[i] };

			for (size_t j = 1; j < objects.size(); j++)
			{
				size_t seen = 0;

				for (ComponentBase* candidate : objects[j]->GetComponents())
					if (SameKind(*candidate, *own[i]) && seen++ == along)
					{
						matches.push_back(candidate);
						break;
					};

				if (matches.size() != j + 1)
					break;
			};

			if (matches.size() == objects.size())
				common.push_back(std::move(matches));
		};

		return common;
	};

	#pragma endregion

	#pragma region Fields

	size_t MultiEdit::AxisCount(const LoomObject& object, size_t index)
	{
		constexpr size_t vec2 = 2;
		constexpr size_t vec3 = 3;
		constexpr size_t vec4 = 4;

		switch (object.GetFields()[index].type)
		{
		case FieldType::Vec2:	return vec2;
		case FieldType::Vec3:	return vec3;
		case FieldType::Vec4:	return vec4;
		default:				return 0;
		};
	};

	bool MultiEdit::Mixed(const std::vector<LoomObject*>& objects, size_t index)
	{
		if (objects.size() < 2)
			return false;

		const std::string first = objects.front()->GetFields()[index].Write();

		return std::any_of(
			objects.begin() + 1,
			objects.end(),
			[&](const LoomObject* other) { return other->GetFields()[index].Write() != first; });
	};

	MultiEdit::Axes MultiEdit::MixedAxes(const std::vector<LoomObject*>& objects, size_t index)
	{
		Axes mixed{ };

		if (objects.empty())
			return mixed;

		const size_t count = AxisCount(*objects.front(), index);
		const float* first = (const float*)objects.front()->GetFields()[index].data;

		for (size_t i = 1; i < objects.size(); i++)
		{
			const float* other = (const float*)objects[i]->GetFields()[index].data;

			for (size_t axis = 0; axis < count; axis++)
				mixed[axis] = mixed[axis] || other[axis] != first[axis];
		};

		return mixed;
	};

	void MultiEdit::Spread(const std::vector<LoomObject*>& objects, size_t index, const float* before)
	{
		if (objects.size() < 2)
			return;

		const SerializedField& source = objects.front()->GetFields()[index];
		const size_t count = before ? AxisCount(*objects.front(), index) : 0;
		const std::string text = count == 0 ? source.Write() : std::string();

		for (size_t i = 1; i < objects.size(); i++)
		{
			const SerializedField& field = objects[i]->GetFields()[index];

			if (count == 0)
				field.Read(text);
			else
			{
				const float* from = (const float*)source.data;
				float* to = (float*)field.data;

				for (size_t axis = 0; axis < count; axis++)
					if (from[axis] != before[axis])
						to[axis] = from[axis];

				field.Changed();
			};

			objects[i]->OnFieldChanged(field);
		};
	};

	void MultiEdit::Shift(const std::vector<LoomObject*>& objects, size_t index, const float* before)
	{
		const size_t count = AxisCount(*objects.front(), index);
		const float* from = (const float*)objects.front()->GetFields()[index].data;

		for (size_t i = 1; i < objects.size(); i++)
		{
			const SerializedField& field = objects[i]->GetFields()[index];
			float* to = (float*)field.data;

			for (size_t axis = 0; axis < count; axis++)
				to[axis] += from[axis] - before[axis];

			field.Changed();
			objects[i]->OnFieldChanged(field);
		};
	};

	#pragma endregion
};
