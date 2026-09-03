#include "doctest.h"

#include "Test Support.h"

#include "Collider.h"

#include <type_traits>


TEST_SUITE("Collider")
{
	// Collider is not constructed anywhere in these tests, and that is
	// deliberate. Its constructor reads m_gameObject:
	//
	//     Collider::Collider() : m_mesh(m_gameObject->GetComponent<Mesh>())
	//
	// but m_gameObject is only assigned by GameObject::Attach, on the task
	// queue, well after the component has been built. So the pointer is
	// uninitialised at that point and the constructor dereferences it.
	// Attach<Collider>() would take the process down before any assertion here
	// could run. Once the constructor stops looking the mesh up eagerly (or
	// Attach starts wiring the parent before construction), the behaviour tests
	// below the type checks can be filled in.

	TEST_CASE("Collider is a component the physics pass can drive")
	{
		CHECK(std::is_base_of_v<Loom::Component<Loom::Physics::Collider>, Loom::Physics::Collider>);
		CHECK(std::is_base_of_v<Loom::ComponentBase, Loom::Physics::Collider>);
	};

	TEST_CASE("Collider overrides OnPhysics and nothing else")
	{
		// GameObject::Attach decides which per-frame lists a component joins by
		// comparing its callbacks against the base ones, so which of these are
		// overridden is part of the contract.
		// Same test the engine's overrides_on_* traits make: an override changes
		// the class the member pointer is qualified by.
		CHECK_FALSE(std::is_same_v<
			decltype(&Loom::Physics::Collider::OnPhysics),
			decltype(&Loom::ComponentBase::OnPhysics)>);

		CHECK(std::is_same_v<
			decltype(&Loom::Physics::Collider::OnUpdate),
			decltype(&Loom::ComponentBase::OnUpdate)>);

		CHECK(std::is_same_v<
			decltype(&Loom::Physics::Collider::OnRender),
			decltype(&Loom::ComponentBase::OnRender)>);
	};

	TEST_CASE("CubeCollider is still a placeholder")
	{
		// Empty today. Here so that giving it state or behaviour has an obvious
		// place to be tested from.
		CHECK(std::is_empty_v<Loom::Physics::CubeCollider>);
		CHECK(std::is_default_constructible_v<Loom::Physics::CubeCollider>);
	};
};
