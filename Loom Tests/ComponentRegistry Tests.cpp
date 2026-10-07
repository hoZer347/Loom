#include "doctest.h"

#include "Test Support.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Material.h"
#include "Mesh.h"
#include "Scene.h"

#include <cstring>
#include <string>
#include <vector>

using LoomTests::Pump;

namespace
{
	struct Registered final : Loom::Component<Registered>
	{
		Registered() = default;

		explicit Registered(int given) :
			value(given)
		{ };

		int value = 0;
	};

	// Neither of these is registered by anything in this file.
	struct SelfRegistered final : Loom::Component<SelfRegistered>
	{ };

	struct NeedsArguments final : Loom::Component<NeedsArguments>
	{
		explicit NeedsArguments(int) { };
	};

	// Both are called "Clash" once the namespaces are dropped.
	namespace First
	{
		struct Clash final : Loom::Component<Clash>
		{ };
	};

	namespace Second
	{
		struct Clash final : Loom::Component<Clash>
		{ };
	};
};


TEST_SUITE("ComponentRegistry")
{
	// The registry is what turns "Component Mesh(...)" in a scene file back into
	// a Mesh, and what fills the editor's Add Component menu.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a registered type can be created by name")
	{
		Loom::ComponentRegistry::Register<Registered>("Registered");

		Loom::Scene scene("by name");
		Pump();

		Loom::ComponentBase* component =
			Loom::ComponentRegistry::Create("Registered", scene.GetRoot());

		REQUIRE(component != nullptr);
		Pump();

		CHECK(scene.GetRoot().GetComponent<Registered>() == component);

		Loom::ComponentRegistry::Unregister("Registered");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "constructor arguments are kept with the type")
	{
		Loom::ComponentRegistry::Register<Registered>("Constructed", 11);

		Loom::Scene scene("with arguments");
		Pump();

		Loom::ComponentBase* component =
			Loom::ComponentRegistry::Create("Constructed", scene.GetRoot());

		REQUIRE(component != nullptr);
		Pump();

		CHECK(((Registered*)component)->value == 11);

		Loom::ComponentRegistry::Unregister("Constructed");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an unregistered name creates nothing")
	{
		Loom::Scene scene("missing");
		Pump();

		CHECK(Loom::ComponentRegistry::Create("NotRegistered", scene.GetRoot()) == nullptr);
		CHECK(scene.GetRoot().GetComponents().empty());
	};

	// A script library is unloaded before it is rebuilt, and a factory left
	// pointing into code that is no longer mapped is a crash waiting for someone
	// to open the Add Component menu.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "unregistering takes the type back out")
	{
		Loom::ComponentRegistry::Register<Registered>("Temporary");

		CHECK(Loom::ComponentRegistry::All().count("Temporary") == 1);

		Loom::ComponentRegistry::Unregister("Temporary");

		CHECK(Loom::ComponentRegistry::All().count("Temporary") == 0);

		Loom::Scene scene("after unregister");
		Pump();

		CHECK(Loom::ComponentRegistry::Create("Temporary", scene.GetRoot()) == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a component reports the name it was registered under")
	{
		Loom::ComponentRegistry::Register<Registered>("Nameable");

		Loom::Scene scene("names");
		Pump();

		Loom::ComponentBase* component =
			Loom::ComponentRegistry::Create("Nameable", scene.GetRoot());

		REQUIRE(component != nullptr);
		Pump();

		CHECK(Loom::ComponentRegistry::NameOf(*component) == "Nameable");

		Loom::ComponentRegistry::Unregister("Nameable");

		// Without a registration it falls back to the type's own name, tidied up.
		CHECK(Loom::ComponentRegistry::NameOf(*component) == "Registered");
	};

	// The fallback name ends up in scene files and in the built-in GUI, and the
	// two compilers this builds with do not spell typeid().name() the same way:
	// MSVC writes it out, clang hands back the mangled form.
	TEST_CASE("a type name is tidied up whichever compiler spelled it")
	{
		CHECK(Loom::PrettyTypeName("struct Loom::Mesh") == "Mesh");
		CHECK(Loom::PrettyTypeName("class Loom::Physics::Collider") == "Collider");
		CHECK(Loom::PrettyTypeName("struct Example") == "Example");

		CHECK(Loom::PrettyTypeName("N4Loom4MeshE") == "Mesh");
		CHECK(Loom::PrettyTypeName("N12_GLOBAL__N_110RegisteredE") == "Registered");
		CHECK(Loom::PrettyTypeName("7Example") == "Example");

		CHECK(Loom::PrettyTypeName(nullptr) == "<unknown>");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "deriving from Component is enough to be registered")
	{
		REQUIRE(Loom::ComponentRegistry::All().count("SelfRegistered") == 1);

		Loom::Scene scene("self registered");
		Pump();

		Loom::ComponentBase* component =
			Loom::ComponentRegistry::Create("SelfRegistered", scene.GetRoot());

		REQUIRE(component != nullptr);
		Pump();

		CHECK(scene.GetRoot().GetComponent<SelfRegistered>() == component);
		CHECK(Loom::ComponentRegistry::NameOf(*component) == "SelfRegistered");
	};

	// Nothing could fill in the arguments from a menu or a scene file.
	TEST_CASE("a component that needs arguments is not registered")
	{
		CHECK(Loom::ComponentRegistry::All().count("NeedsArguments") == 0);
	};

	// Which of the two got there first is up to static initialisation; what
	// matters is that neither a second module registering the same type nor a
	// different type with the same name changes what the name builds.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a name stays with the type that took it first")
	{
		Loom::Scene scene("clash");
		Pump();

		Loom::ComponentBase* before = Loom::ComponentRegistry::Create("Clash", scene.GetRoot());

		REQUIRE(before != nullptr);

		Loom::ComponentRegistry::RegisterType<First::Clash>();
		Loom::ComponentRegistry::RegisterType<Second::Clash>();

		Loom::ComponentBase* after = Loom::ComponentRegistry::Create("Clash", scene.GetRoot());

		REQUIRE(after != nullptr);
		Pump();

		CHECK(strcmp(before->GetTypeName(), after->GetTypeName()) == 0);
	};

	TEST_CASE("the engine's own components are registered")
	{
		CHECK(Loom::ComponentRegistry::All().count("Mesh") == 1);
		CHECK(Loom::ComponentRegistry::All().count("Material") == 1);
	};

	// Each is one click away under Add Component, on whatever object is selected.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "every registered component attaches to a bare GameObject")
	{
		std::vector<std::string> names{ };

		for (const auto& [name, factory] : Loom::ComponentRegistry::All())
			names.push_back(name);

		for (const std::string& name : names)
		{
			CAPTURE(name);

			Loom::Scene scene(name);
			Pump();

			Loom::ComponentBase* component =
				Loom::ComponentRegistry::Create(name, scene.GetRoot());

			REQUIRE(component != nullptr);
			Pump();

			CHECK(Loom::ComponentRegistry::NameOf(*component) == name);
		};
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a built-in comes out as the type it says it is")
	{
		Loom::Scene scene("builtins");
		Pump();

		Loom::ComponentBase* mesh =
			Loom::ComponentRegistry::Create("Mesh", scene.GetRoot());

		REQUIRE(mesh != nullptr);
		Pump();

		CHECK(scene.GetRoot().GetComponent<Loom::Mesh>() == mesh);
		CHECK(Loom::ComponentRegistry::NameOf(*mesh) == "Mesh");
	};
};
