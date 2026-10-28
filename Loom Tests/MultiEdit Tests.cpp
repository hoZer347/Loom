#include "doctest.h"

#include "Test Support.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "MultiEdit.h"
#include "Scene.h"

#include "glm/glm.hpp"

#include <memory>
#include <string>
#include <vector>

using LoomTests::Pump;

#pragma region Helpers

namespace
{
	struct Marked final : Loom::Component<Marked>
	{
		Loom::Serial<std::string> label = "unset";
		Loom::Serial<glm::vec3> offset{ };

		int changes = 0;

		void OnFieldChanged(const Loom::SerializedField&) override
		{
			changes++;
		};
	};

	struct Other final : Loom::Component<Other>
	{ };

	struct Selected : LoomTests::EngineFixture
	{
		Selected()
		{
			Loom::ComponentRegistry::Register<Marked>("Marked");
			Loom::ComponentRegistry::Register<Other>("Other");
			Pump();
		};

		~Selected()
		{
			scene.reset();
			Pump();
			Loom::ComponentRegistry::Unregister("Marked");
			Loom::ComponentRegistry::Unregister("Other");
		};

		Loom::GameObject* Add(Loom::GameObject& parent, const std::string& name)
		{
			Loom::GameObject* added = parent.AddChild(name);
			Pump();
			return added;
		};

		template <typename T>
		T* Attach(Loom::GameObject& gameObject)
		{
			T* attached = gameObject.Attach<T>();
			Pump();
			return attached;
		};

		std::unique_ptr<Loom::Scene> scene = std::make_unique<Loom::Scene>("Selected");
	};

	size_t IndexOf(const Loom::LoomObject& object, const void* data)
	{
		const std::vector<Loom::SerializedField>& fields = object.GetFields();

		for (size_t i = 0; i < fields.size(); i++)
			if (fields[i].data == data)
				return i;

		return fields.size();
	};
};

#pragma endregion


TEST_SUITE("MultiEdit")
{
	#pragma region Selection

	TEST_CASE_FIXTURE(Selected, "rows follow the hierarchy's order and skip what is folded")
	{
		Loom::GameObject& root = scene->GetRoot();
		Loom::GameObject* a = Add(root, "A");
		Loom::GameObject* child = Add(*a, "A child");
		Loom::GameObject* b = Add(root, "B");

		const auto all = [](const Loom::GameObject&) { return true; };
		const auto none = [](const Loom::GameObject&) { return false; };

		CHECK(Loom::MultiEdit::Rows(root, all) == std::vector<Loom::GameObject*>{ a, child, b });
		CHECK(Loom::MultiEdit::Rows(root, none) == std::vector<Loom::GameObject*>{ a, b });
	};

	TEST_CASE_FIXTURE(Selected, "a range runs either way and is empty off the rows")
	{
		Loom::GameObject& root = scene->GetRoot();
		Loom::GameObject* a = Add(root, "A");
		Loom::GameObject* b = Add(root, "B");
		Loom::GameObject* c = Add(root, "C");

		const std::vector<Loom::GameObject*> rows{ a, b, c };

		CHECK(Loom::MultiEdit::Range(rows, a, c) == rows);
		CHECK(Loom::MultiEdit::Range(rows, c, b) == std::vector<Loom::GameObject*>{ b, c });
		CHECK(Loom::MultiEdit::Range(rows, b, b) == std::vector<Loom::GameObject*>{ b });
		CHECK(Loom::MultiEdit::Range(rows, a, &root).empty());
	};

	TEST_CASE_FIXTURE(Selected, "tops drop what sits under another selected object")
	{
		Loom::GameObject& root = scene->GetRoot();
		Loom::GameObject* a = Add(root, "A");
		Loom::GameObject* grandchild = Add(*Add(*a, "A child"), "A grandchild");
		Loom::GameObject* b = Add(root, "B");

		CHECK(Loom::MultiEdit::Tops({ grandchild, a, b }) == std::vector<Loom::GameObject*>{ a, b });
		CHECK(Loom::MultiEdit::Tops({ grandchild, b }) == std::vector<Loom::GameObject*>{ grandchild, b });
	};

	#pragma endregion

	#pragma region Components

	TEST_CASE_FIXTURE(Selected, "only components every object has are common, matched by type and place")
	{
		Loom::GameObject& root = scene->GetRoot();
		Loom::GameObject* a = Add(root, "A");
		Loom::GameObject* b = Add(root, "B");

		Marked* a_first = Attach<Marked>(*a);
		Attach<Other>(*a);
		Attach<Marked>(*a);

		Marked* b_first = Attach<Marked>(*b);

		const std::vector<std::vector<Loom::ComponentBase*>> common = Loom::MultiEdit::Common({ a, b });

		REQUIRE(common.size() == 1);
		CHECK(common.front() == std::vector<Loom::ComponentBase*>{ a_first, b_first });
		CHECK(Loom::MultiEdit::Common({ a }).size() == a->GetComponents().size());
	};

	#pragma endregion

	#pragma region Fields

	TEST_CASE_FIXTURE(Selected, "a field differing between objects is mixed, a vector per axis")
	{
		Loom::GameObject& root = scene->GetRoot();
		Marked* a = Attach<Marked>(*Add(root, "A"));
		Marked* b = Attach<Marked>(*Add(root, "B"));

		const size_t label = IndexOf(*a, &(const std::string&)a->label);
		const size_t offset = IndexOf(*a, &(const glm::vec3&)a->offset);
		const std::vector<Loom::LoomObject*> objects{ a, b };

		CHECK_FALSE(Loom::MultiEdit::Mixed(objects, label));

		b->label = "other";
		b->offset = glm::vec3(0.0f, 2.0f, 0.0f);

		CHECK(Loom::MultiEdit::Mixed(objects, label));
		CHECK(Loom::MultiEdit::AxisCount(*a, offset) == 3);
		CHECK(Loom::MultiEdit::AxisCount(*a, label) == 0);
		CHECK(Loom::MultiEdit::MixedAxes(objects, offset) == Loom::MultiEdit::Axes{ false, true, false, false });
	};

	TEST_CASE_FIXTURE(Selected, "spreading copies the first's value, and only the axes it changed")
	{
		Loom::GameObject& root = scene->GetRoot();
		Marked* a = Attach<Marked>(*Add(root, "A"));
		Marked* b = Attach<Marked>(*Add(root, "B"));

		const size_t label = IndexOf(*a, &(const std::string&)a->label);
		const size_t offset = IndexOf(*a, &(const glm::vec3&)a->offset);
		const std::vector<Loom::LoomObject*> objects{ a, b };

		a->label = "both";
		Loom::MultiEdit::Spread(objects, label);

		CHECK((const std::string&)b->label == "both");
		CHECK(b->changes == 1);

		a->offset = glm::vec3(1.0f, 0.0f, 0.0f);
		b->offset = glm::vec3(0.0f, 5.0f, 0.0f);

		const glm::vec3 before = a->offset;
		a->offset = glm::vec3(1.0f, 0.0f, 7.0f);
		Loom::MultiEdit::Spread(objects, offset, &before.x);

		CHECK((const glm::vec3&)b->offset == glm::vec3(0.0f, 5.0f, 7.0f));
		CHECK(b->changes == 2);
		CHECK(a->changes == 0);
	};

	TEST_CASE_FIXTURE(Selected, "shifting moves the rest by as much as the first moved, so each keeps its own value")
	{
		Loom::GameObject& root = scene->GetRoot();
		Marked* a = Attach<Marked>(*Add(root, "A"));
		Marked* b = Attach<Marked>(*Add(root, "B"));

		const size_t offset = IndexOf(*a, &(const glm::vec3&)a->offset);
		const std::vector<Loom::LoomObject*> objects{ a, b };

		a->offset = glm::vec3(1.0f, 0.0f, 0.0f);
		b->offset = glm::vec3(0.0f, 5.0f, 2.0f);

		const glm::vec3 before = a->offset;
		a->offset = glm::vec3(3.0f, 0.0f, -1.0f);
		Loom::MultiEdit::Shift(objects, offset, &before.x);

		CHECK((const glm::vec3&)b->offset == glm::vec3(2.0f, 5.0f, 1.0f));
		CHECK(b->changes == 1);
		CHECK(a->changes == 0);
	};

	#pragma endregion
};
