#include "doctest.h"

#include "Test Support.h"

#include "Component.h"
#include "Scene.h"

#include <string>
#include <type_traits>
#include <typeinfo>

using LoomTests::Pump;
using LoomTests::Probe;

// Every component is a LoomObject, and a LoomObject queues its own registration
// on construction. So each case here pumps immediately after building one: that
// consumes the queued work while the object is still alive, instead of leaving
// it to fire against freed memory in a later test.


TEST_SUITE("Component")
{
	// The engine calls every one of these on components that do not override
	// them, so the base versions have to be safe no-ops rather than pure virtuals.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the base callbacks are no-ops, not requirements")
	{
		LoomTests::InertComponent inert;
		Pump();

		inert.OnAttach();
		inert.OnDetach();
		inert.OnUpdate();
		inert.OnRender();
		inert.OnPhysics();
		inert.OnGui();

		CHECK(true); // Calling all six with no GameObject and no GL context is the assertion.
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an overriding component receives its callbacks through the base interface")
	{
		Probe<> probe;
		Pump();

		Loom::ComponentBase& asBase = probe;

		asBase.OnAttach();
		asBase.OnUpdate();
		asBase.OnUpdate();
		asBase.OnRender();
		asBase.OnPhysics();
		asBase.OnDetach();

		CHECK(probe.Count("attach")  == 1);
		CHECK(probe.Count("update")  == 2);
		CHECK(probe.Count("render")  == 1);
		CHECK(probe.Count("physics") == 1);
		CHECK(probe.Count("detach")  == 1);
	};

	// GameObject only ever owns ComponentBase*, so the base destructor has to be
	// virtual or every component leaks its derived part.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a component is destroyed through the base pointer the engine holds")
	{
		CHECK(std::has_virtual_destructor_v<Loom::ComponentBase>);

		Loom::ComponentBase* component = new Probe<>();
		Pump();

		delete component;
		Pump();

		CHECK(true);
	};

	// ComponentBase::m_gameObject has no initialiser, so an unattached component
	// does not hold null -- it holds whatever was on the stack or in the heap
	// block. Reading it before Attach has run is undefined, which is why this
	// case checks the attached side of the contract instead of the unattached
	// one. Giving m_gameObject a "= nullptr" would make "no parent yet" a
	// question a component could actually answer.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a component's parent is the GameObject that attached it")
	{
		Loom::Scene scene("component parent");
		Pump();

		Probe<>* probe = scene.Attach<Probe<>>();
		Pump();

		CHECK(probe->GetGameObject() == &scene.GetRoot());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "GetClassName reports the object name, not the C++ type")
	{
		// Worth pinning down because the name suggests otherwise: this is
		// LoomObject's name field, which is empty until something sets it.
		Probe<> probe;
		Pump();

		CHECK(probe.GetClassName().empty());

		probe.SetName("Player Probe");

		CHECK(probe.GetClassName() == "Player Probe");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a component is a LoomObject and so carries its own ID")
	{
		Probe<'A'> first;
		Probe<'B'> second;
		Pump();

		CHECK(first.m_ID != second.m_ID);
	};

	// Attach and GetComponent identify components by typeid, so two probes with
	// different tags have to be genuinely different types.
	TEST_CASE("each Component<T> instantiation is a distinct type")
	{
		CHECK(typeid(Probe<'A'>) != typeid(Probe<'B'>));
		CHECK(typeid(LoomTests::InertComponent) != typeid(LoomTests::UpdateOnlyComponent));
	};

	// The CRTP parameter is what lets GetComponent<T> find the component again,
	// so it has to name the component's own type.
	TEST_CASE("a component sits on the LoomObject / ComponentBase / Component<T> chain")
	{
		CHECK(std::is_base_of_v<Loom::Component<Probe<>>, Probe<>>);
		CHECK(std::is_base_of_v<Loom::ComponentBase, Probe<>>);
		CHECK(std::is_base_of_v<Loom::LoomObject, Probe<>>);
	};

	// Component<T> exists only to be derived from -- GameObject::Attach builds
	// the concrete type and stores it as a ComponentBase.
	TEST_CASE("ComponentBase is not something an application constructs directly")
	{
		CHECK(std::is_abstract_v<Loom::ComponentBase> == false);
		CHECK_FALSE(std::is_constructible_v<Loom::Component<Probe<>>>);
	};
};
