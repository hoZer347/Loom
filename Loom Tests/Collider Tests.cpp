#include "doctest.h"

#include "Test Support.h"

#include "Collider.h"

#include <type_traits>


TEST_SUITE("Collider")
{
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
