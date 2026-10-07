#include "doctest.h"

#include "Test Support.h"

#include "Scene.h"
#include "GameObject.h"

#include "glm/glm.hpp"

#include <algorithm>
#include <string>
#include <vector>

using LoomTests::Pump;
using LoomTests::Probe;

namespace
{
	// GameObject's constructor is private to Scene and Engine, so every test
	// here reaches a GameObject the way an application does: through a Scene.
	//
	// DetachComponent deletes the component it removes, so a component cannot
	// report its own detach afterwards; this counter outlives it.
	int detachCount = 0;

	struct DetachReporter final : Loom::Component<DetachReporter>
	{
		void OnDetach() override { detachCount++; };
		void OnUpdate() override { };
	};
};


TEST_SUITE("GameObject")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene root starts with no children and no components")
	{
		Loom::Scene scene("root shape");
		Pump();

		CHECK(scene.GetRoot().GetChildren().empty());
		CHECK(scene.GetRoot().GetComponents().empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Attach owns the component at once and defers running it")
	{
		Loom::Scene scene("attach");
		Pump();

		Probe<>* probe = scene.GetRoot().Attach<Probe<>>();

		REQUIRE(probe != nullptr);

		// On the object straight away, so anything looking for it finds it.
		CHECK(scene.GetRoot().GetComponents().size() == 1);
		CHECK(scene.GetRoot().GetComponent<Probe<>>() == probe);
		CHECK(probe->GetGameObject() == &scene.GetRoot());

		// What waits for the queue is the work that runs it.
		CHECK(probe->Count("attach") == 0);

		Pump();

		CHECK(probe->Count("attach") == 1);
		CHECK(scene.GetRoot().GetComponents().size() == 1);
	};

	// Two components created in one pass - a scene load, or one component's
	// OnAttach reaching for another - have to be able to see each other. When
	// ownership waited for the queue they could not, and a script asking for a
	// sibling got nothing and attached a second one.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "components attached in the same frame are siblings")
	{
		Loom::Scene scene("siblings");
		Pump();

		Probe<'A'>* first = scene.GetRoot().Attach<Probe<'A'>>();
		Probe<'B'>* second = scene.GetRoot().Attach<Probe<'B'>>();

		CHECK(scene.GetRoot().GetComponent<Probe<'A'>>() == first);
		CHECK(scene.GetRoot().GetComponent<Probe<'B'>>() == second);

		Pump();

		CHECK(scene.GetRoot().GetComponents().size() == 2);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Attach calls OnAttach exactly once")
	{
		Loom::Scene scene("on attach");
		Pump();

		Probe<>* probe = scene.GetRoot().Attach<Probe<>>();
		Pump();
		Pump();

		CHECK(probe->Count("attach") == 1);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Attach forwards constructor arguments to the component")
	{
		Loom::Scene scene("attach args");
		Pump();

		LoomTests::ConstructedComponent* made =
			scene.GetRoot().Attach<LoomTests::ConstructedComponent>(7, std::string("seven"));

		Pump();

		REQUIRE(made != nullptr);
		CHECK(made->number == 7);
		CHECK(made->text == "seven");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "GetComponent tells component types apart")
	{
		Loom::Scene scene("component lookup");
		Pump();

		Probe<'A'>* a = scene.GetRoot().Attach<Probe<'A'>>();
		Probe<'B'>* b = scene.GetRoot().Attach<Probe<'B'>>();
		Pump();

		CHECK(scene.GetRoot().GetComponent<Probe<'A'>>() == a);
		CHECK(scene.GetRoot().GetComponent<Probe<'B'>>() == b);
		CHECK(scene.GetRoot().GetComponent<Probe<'C'>>() == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "GetComponent returns null when nothing is attached")
	{
		Loom::Scene scene("empty lookup");
		Pump();

		CHECK(scene.GetRoot().GetComponent<Probe<>>() == nullptr);
	};

	// Attach inspects each callback at compile time and only registers the
	// component on the lists it actually implements, so a component that
	// overrides OnUpdate is ticked but never drawn.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a component is only ticked on the callbacks it overrides")
	{
		Loom::Scene scene("selective registration");
		Pump();

		LoomTests::UpdateOnlyComponent* updater =
			scene.GetRoot().Attach<LoomTests::UpdateOnlyComponent>();
		Pump();

		scene.Render();
		CHECK(updater->updates == 0);

		scene.Update();
		CHECK(updater->updates == 1);

		scene.Update();
		CHECK(updater->updates == 2);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a component that overrides nothing is owned but never called")
	{
		Loom::Scene scene("inert component");
		Pump();

		LoomTests::InertComponent* inert = scene.GetRoot().Attach<LoomTests::InertComponent>();
		Pump();

		CHECK(scene.GetRoot().GetComponent<LoomTests::InertComponent>() == inert);

		scene.Update();
		scene.Render();
		scene.Physics();

		CHECK(true); // Ticking a component with no overrides must not reach into GL.
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "AddChild builds a hierarchy")
	{
		Loom::Scene scene("hierarchy");
		Pump();

		Loom::GameObject* child = scene.GetRoot().AddChild("Child");

		// Like Attach, the parent does not own the child until the queue drains.
		CHECK(scene.GetRoot().GetChildren().empty());

		Pump();

		REQUIRE(scene.GetRoot().GetChildren().size() == 1);
		CHECK(scene.GetRoot().GetChildren()[0] == child);
		CHECK(child->GetName() == "Child");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "AddChild defaults the name")
	{
		Loom::Scene scene("default child name");
		Pump();

		Loom::GameObject* child = scene.GetRoot().AddChild();
		Pump();

		CHECK(child->GetName() == "New GameObject");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Update walks the whole hierarchy, not just the root")
	{
		Loom::Scene scene("deep update");
		Pump();

		Loom::GameObject* child = scene.GetRoot().AddChild("Child");
		Pump();

		Loom::GameObject* grandchild = child->AddChild("Grandchild");
		Pump();

		Probe<>* onRoot       = scene.GetRoot().Attach<Probe<>>();
		Probe<>* onChild      = child->Attach<Probe<>>();
		Probe<>* onGrandchild = grandchild->Attach<Probe<>>();
		Pump();

		scene.Update();

		CHECK(onRoot->Count("update")       == 1);
		CHECK(onChild->Count("update")      == 1);
		CHECK(onGrandchild->Count("update") == 1);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Render reaches the hierarchy too")
	{
		Loom::Scene scene("deep render");
		Pump();

		Loom::GameObject* child = scene.GetRoot().AddChild("Child");
		Pump();

		Probe<>* probe = child->Attach<Probe<>>();
		Pump();

		scene.Render();

		CHECK(probe->Count("render") == 1);
	};

	// Attach fills m_components, m_updateables and m_renderables, but never
	// m_physicsables -- so GameObject::Physics walks an always-empty list and no
	// component's OnPhysics is ever called, on any object, anywhere. Collider,
	// whose whole interface is OnPhysics, is dead code because of it.
	//
	// Marked should_fail: this starts passing the moment Attach registers
	// physics components, and doctest will say so.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Physics reaches the hierarchy too" * doctest::should_fail())
	{
		Loom::Scene scene("deep physics");
		Pump();

		Loom::GameObject* child = scene.GetRoot().AddChild("Child");
		Pump();

		Probe<>* probe = child->Attach<Probe<>>();
		Pump();

		scene.Physics();

		CHECK(probe->Count("physics") == 1);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "RemoveChild detaches and deletes the child")
	{
		Loom::Scene scene("remove child");
		Pump();

		Loom::GameObject* child = scene.GetRoot().AddChild("Doomed");
		Pump();

		REQUIRE(scene.GetRoot().GetChildren().size() == 1);

		scene.GetRoot().RemoveChild(child);
		Pump();

		CHECK(scene.GetRoot().GetChildren().empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Destroy removes a child from its parent")
	{
		Loom::Scene scene("destroy child");
		Pump();

		Loom::GameObject* child = scene.GetRoot().AddChild("Doomed");
		Pump();

		REQUIRE(scene.GetRoot().GetChildren().size() == 1);

		child->Destroy();
		Pump();

		CHECK(scene.GetRoot().GetChildren().empty());
	};

	// A scene's root has no parent to be removed from, so destroying it would
	// leave the scene without a hierarchy. The engine refuses instead.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Destroy refuses to delete a scene root")
	{
		Loom::Scene scene("destroy root");
		Pump();

		Loom::GameObject* child = scene.GetRoot().AddChild("Survivor");
		Pump();

		scene.GetRoot().Destroy();
		Pump();

		CHECK(scene.GetRoot().GetChildren().size() == 1);
		CHECK(scene.GetRoot().GetChildren()[0] == child);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "DetachComponent removes one instance and calls OnDetach")
	{
		Loom::Scene scene("detach");
		Pump();

		detachCount = 0;

		DetachReporter* reporter = scene.GetRoot().Attach<DetachReporter>();
		Pump();

		REQUIRE(scene.GetRoot().GetComponents().size() == 1);

		scene.GetRoot().DetachComponent(reporter);
		Pump();

		CHECK(scene.GetRoot().GetComponents().empty());
		CHECK(scene.GetRoot().GetComponent<DetachReporter>() == nullptr);
		CHECK(detachCount == 1);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a detached component stops being ticked")
	{
		Loom::Scene scene("detach stops updates");
		Pump();

		LoomTests::UpdateOnlyComponent* updater =
			scene.GetRoot().Attach<LoomTests::UpdateOnlyComponent>();
		Pump();

		scene.Update();
		REQUIRE(updater->updates == 1);

		scene.GetRoot().DetachComponent(updater);
		Pump();

		scene.Update();

		CHECK(scene.GetRoot().GetComponents().empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "DetachComponent ignores null")
	{
		Loom::Scene scene("detach null");
		Pump();

		scene.GetRoot().Attach<Probe<>>();
		Pump();

		scene.GetRoot().DetachComponent(nullptr);
		Pump();

		CHECK(scene.GetRoot().GetComponents().size() == 1);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "DetachComponent ignores a component owned by someone else")
	{
		Loom::Scene scene("detach foreign");
		Pump();

		Loom::GameObject* child = scene.GetRoot().AddChild("Owner");
		Pump();

		detachCount = 0;

		DetachReporter* onChild = child->Attach<DetachReporter>();
		Pump();

		scene.GetRoot().DetachComponent(onChild);
		Pump();

		CHECK(child->GetComponents().size() == 1);
		CHECK(detachCount == 0);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "SetThreadID carries down to the children that inherit it")
	{
		constexpr int ROOT_THREAD = 2;
		constexpr int OWN_THREAD = 3;

		Loom::Scene scene("thread ids");
		Pump();

		Loom::GameObject* inheriting = scene.GetRoot().AddChild("Inheriting");
		Loom::GameObject* independent = scene.GetRoot().AddChild("Independent");
		Pump();

		inheriting->SetInheritThreadID(true);
		independent->SetThreadID(OWN_THREAD);

		scene.GetRoot().SetThreadID(ROOT_THREAD);

		CHECK(inheriting->GetThreadID() == ROOT_THREAD);
		CHECK(independent->GetThreadID() == OWN_THREAD);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "turning inheritance on takes the parent's thread at once")
	{
		constexpr int ROOT_THREAD = 2;
		constexpr int OWN_THREAD = 3;

		Loom::Scene scene("inherit");
		Pump();

		Loom::GameObject* child = scene.GetRoot().AddChild("Child");
		Pump();

		scene.GetRoot().SetThreadID(ROOT_THREAD);
		child->SetThreadID(OWN_THREAD);

		child->SetInheritThreadID(true);

		CHECK(child->InheritsThreadID());
		CHECK(child->GetThreadID() == ROOT_THREAD);
	};

	// Scenes saved before the transform existed number the thread fields 0 and
	// 1, so the transform has to come after them.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the transform is serialized after the thread fields")
	{
		constexpr size_t THREAD_FIELDS = 2;
		constexpr size_t TRANSFORM_FIELDS = 3;

		Loom::Scene scene("transform fields");
		Pump();

		Loom::GameObject& root = scene.GetRoot();
		const std::vector<Loom::SerializedField>& fields = root.GetFields();

		REQUIRE(fields.size() == THREAD_FIELDS + TRANSFORM_FIELDS);

		CHECK(fields[0].type == Loom::FieldType::Int);
		CHECK(fields[1].type == Loom::FieldType::Bool);

		for (size_t i = THREAD_FIELDS; i < fields.size(); i++)
			CHECK(fields[i].type == Loom::FieldType::Vec3);

		CHECK(fields[THREAD_FIELDS].data == &root.transform.position->x);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a new transform is the identity")
	{
		Loom::Scene scene("identity transform");
		Pump();

		CHECK(scene.GetRoot().transform.Matrix() == glm::mat4(1.0f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a transform scales, then rotates, then translates")
	{
		constexpr float QUARTER_TURN = 90.0f;

		Loom::Scene scene("composed transform");
		Pump();

		Loom::Transform& transform = scene.GetRoot().transform;

		transform.position = glm::vec3(10.0f, 0.0f, 0.0f);
		transform.rotation = glm::vec3(0.0f, 0.0f, QUARTER_TURN);
		transform.scale = glm::vec3(2.0f);

		// x scaled to 2, turned onto y, then moved along x.
		const glm::vec4 moved = transform.Matrix() * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

		CHECK(moved.x == doctest::Approx(10.0f));
		CHECK(moved.y == doctest::Approx(2.0f));
		CHECK(moved.z == doctest::Approx(0.0f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a GameObject is a LoomObject with its own ID and name")
	{
		Loom::Scene scene("identity");
		Pump();

		Loom::GameObject* first  = scene.GetRoot().AddChild("First");
		Loom::GameObject* second = scene.GetRoot().AddChild("Second");
		Pump();

		CHECK(first->m_ID != second->m_ID);
		CHECK(first->NameAndID() == "First (ID: " + std::to_string(first->m_ID) + ')');
		CHECK(scene.GetRoot().GetName() == "Root");
	};
};
