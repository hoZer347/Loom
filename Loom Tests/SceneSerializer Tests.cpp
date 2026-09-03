#include "doctest.h"

#include "Test Support.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Mesh.h"
#include "Scene.h"
#include "SceneSerializer.h"

#include <memory>
#include <string>

using LoomTests::Pump;

namespace
{
	// A component with something worth writing down, registered under a name the
	// tests can put in a scene file.
	struct Marker final : Loom::Component<Marker>
	{
		Loom::Serial<float> weight = 1.0f;
		Loom::Serial<std::string> label = "unset";
		Loom::Serial<Loom::GameObject*> partner;
	};

	// Registered once for the whole suite: the registry is process-wide, and the
	// loader looks types up in it by name.
	struct MarkerRegistered
	{
		MarkerRegistered()
		{
			Loom::ComponentRegistry::Register<Marker>("Marker");
		};

		~MarkerRegistered()
		{
			Loom::ComponentRegistry::Unregister("Marker");
		};
	};

	// Owns the scene a test loaded, so a failing CHECK cannot leak it.
	struct Loaded
	{
		explicit Loaded(const std::string& text)
		{
			scene.reset(Loom::SceneSerializer::Deserialize(text, &error));
			Pump();
		};

		~Loaded()
		{
			scene.reset();
			Pump();
		};

		std::unique_ptr<Loom::Scene> scene;
		std::string error;
	};

	std::string Written(Loom::Scene& scene)
	{
		Pump();
		return Loom::SceneSerializer::Serialize(scene);
	};
};


TEST_SUITE("SceneSerializer")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an empty scene is a header and a root")
	{
		Loom::Scene scene("Empty");

		const std::string text = Written(scene);

		CHECK(text.rfind("Empty(" + scene.GetGuid().ToString() + "):\n", 0) == 0);
		CHECK(text.find("\tGameObject Root(" + scene.GetRoot().GetGuid().ToString() + ")\n") != std::string::npos);
	};

	// One tab per level is the whole grammar; depth is what says who owns what.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "nesting is one tab per level")
	{
		MarkerRegistered registered;

		Loom::Scene scene("Nested");
		Loom::GameObject* child = scene.AddChild("Child");
		Pump();

		child->Attach<Marker>();
		Pump();

		const std::string text = Written(scene);

		CHECK(text.find("\n\t\tGameObject Child(") != std::string::npos);
		CHECK(text.find("\n\t\t\tComponent Marker(") != std::string::npos);
		CHECK(text.find("\n\t\t\t\t0 = 1") != std::string::npos);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene written and read back is the same scene")
	{
		MarkerRegistered registered;

		std::string text;
		std::string root_guid;
		std::string marker_guid;

		{
			Loom::Scene scene("Round Trip");
			Loom::GameObject* child = scene.AddChild("Child");
			Pump();

			Loom::GameObject* grandchild = child->AddChild("Grandchild");
			Marker* marker = child->Attach<Marker>();
			Pump();

			marker->weight = 2.5f;
			marker->label = "tagged";
			marker->partner = grandchild;

			text = Written(scene);
			root_guid = scene.GetRoot().GetGuid().ToString();
			marker_guid = marker->GetGuid().ToString();
		};

		Pump();

		const Loaded loaded(text);

		REQUIRE(loaded.scene != nullptr);

		CHECK(loaded.scene->GetName() == "Round Trip");
		CHECK(loaded.scene->GetRoot().GetGuid().ToString() == root_guid);

		REQUIRE(loaded.scene->GetRoot().GetChildren().size() == 1);

		Loom::GameObject* child = loaded.scene->GetRoot().GetChildren().front();

		CHECK(child->GetName() == "Child");
		REQUIRE(child->GetChildren().size() == 1);
		REQUIRE(child->GetComponents().size() == 1);

		Marker* marker = child->GetComponent<Marker>();

		REQUIRE(marker != nullptr);

		CHECK(marker->GetGuid().ToString() == marker_guid);
		CHECK(marker->weight == doctest::Approx(2.5f));
		CHECK(*marker->label == "tagged");

		// The reference came back pointing at the same object in the new scene,
		// which is the whole reason objects carry guids.
		CHECK(marker->partner == child->GetChildren().front());

		// And writing it out again produces the same text.
		CHECK(Loom::SceneSerializer::Serialize(*loaded.scene) == text);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a reference to something further down the file still lands")
	{
		MarkerRegistered registered;

		const std::string later = Loom::Guid::New().ToString();

		const std::string text =
			"Forward(" + Loom::Guid::New().ToString() + "):\n"
			"\tGameObject Root(" + Loom::Guid::New().ToString() + ")\n"
			"\t\tGameObject First(" + Loom::Guid::New().ToString() + ")\n"
			"\t\t\tComponent Marker(" + Loom::Guid::New().ToString() + ")\n"
			"\t\t\t\t2 = " + later + "\n"
			"\t\tGameObject Second(" + later + ")\n";

		const Loaded loaded(text);

		REQUIRE(loaded.scene != nullptr);
		REQUIRE(loaded.scene->GetRoot().GetChildren().size() == 2);

		Marker* marker = loaded.scene->GetRoot().GetChildren().front()->GetComponent<Marker>();

		REQUIRE(marker != nullptr);
		CHECK(marker->partner == loaded.scene->GetRoot().GetChildren().back());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "four spaces count as a tab")
	{
		const std::string text =
			"Spaces(" + Loom::Guid::New().ToString() + "):\n"
			"    GameObject Root(" + Loom::Guid::New().ToString() + ")\n"
			"        GameObject Child(" + Loom::Guid::New().ToString() + ")\n";

		const Loaded loaded(text);

		REQUIRE(loaded.scene != nullptr);
		CHECK(loaded.scene->GetRoot().GetChildren().size() == 1);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "blank lines and comments are skipped")
	{
		const std::string text =
			"# a scene\n"
			"\n"
			"Commented(" + Loom::Guid::New().ToString() + "):\n"
			"\n"
			"\t# the root\n"
			"\tGameObject Root(" + Loom::Guid::New().ToString() + ")\n";

		const Loaded loaded(text);

		REQUIRE(loaded.scene != nullptr);
		CHECK(loaded.scene->GetName() == "Commented");
	};

	// A file written by a build that had a component this one does not is worth
	// loading anyway, minus the part that cannot be understood.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an unknown component is skipped, with its fields")
	{
		const std::string text =
			"Unknown(" + Loom::Guid::New().ToString() + "):\n"
			"\tGameObject Root(" + Loom::Guid::New().ToString() + ")\n"
			"\t\tGameObject Child(" + Loom::Guid::New().ToString() + ")\n"
			"\t\t\tComponent NotABuiltType(" + Loom::Guid::New().ToString() + ")\n"
			"\t\t\t\tsomething = 4\n";

		const Loaded loaded(text);

		REQUIRE(loaded.scene != nullptr);
		REQUIRE(loaded.scene->GetRoot().GetChildren().size() == 1);
		CHECK(loaded.scene->GetRoot().GetChildren().front()->GetComponents().empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an unknown field is skipped, and the rest is kept")
	{
		MarkerRegistered registered;

		const std::string text =
			"Fields(" + Loom::Guid::New().ToString() + "):\n"
			"\tGameObject Root(" + Loom::Guid::New().ToString() + ")\n"
			"\t\tComponent Marker(" + Loom::Guid::New().ToString() + ")\n"
			"\t\t\t9 = 9\n"
			"\t\t\t1 = \"kept\"\n";

		const Loaded loaded(text);

		REQUIRE(loaded.scene != nullptr);

		Marker* marker = loaded.scene->GetRoot().GetComponent<Marker>();

		REQUIRE(marker != nullptr);
		CHECK(*marker->label == "kept");
	};

	// Fields used to be keyed by member name. A file from then still opens: the
	// lines nothing can be done with are dropped, and the rest still lands.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a field key that is not an index is skipped")
	{
		MarkerRegistered registered;

		const std::string text =
			"Named(" + Loom::Guid::New().ToString() + "):\n"
			"\tGameObject Root(" + Loom::Guid::New().ToString() + ")\n"
			"\t\tComponent Marker(" + Loom::Guid::New().ToString() + ")\n"
			"\t\t\tweight = 4.5\n"
			"\t\t\t18446744073709551616 = 9.5\n"
			"\t\t\t1 = \"still read\"\n";

		const Loaded loaded(text);

		REQUIRE(loaded.scene != nullptr);

		Marker* marker = loaded.scene->GetRoot().GetComponent<Marker>();

		REQUIRE(marker != nullptr);

		// Neither the named line nor the one whose key is 2^64 - which wraps onto
		// field zero, this member, if the digits are not counted first - went
		// anywhere, so it kept its default.
		CHECK(marker->weight == doctest::Approx(1.0f));
		CHECK(*marker->label == "still read");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a file that is not a scene is refused")
	{
		const Loaded loaded("just some text\n");

		CHECK(loaded.scene == nullptr);
		CHECK(loaded.error.find("scene header") != std::string::npos);
	};

	// A hand-written scene file is going to have a typo in it eventually, and a
	// typo has to be a message rather than a crash: everything built before the
	// bad line still has deferred work pointing at it.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a line the parser cannot read fails without taking the process with it")
	{
		const std::string text =
			"Broken(" + Loom::Guid::New().ToString() + "):\n"
			"\tGameObject Root(" + Loom::Guid::New().ToString() + ")\n"
			"\t\tGameObject Child(" + Loom::Guid::New().ToString() + ")\n"
			"\t\toops\n";

		const Loaded loaded(text);

		CHECK(loaded.scene == nullptr);
		CHECK(loaded.error.find("line 4") != std::string::npos);

		// The crash this guards against happens on the next drain, not on the
		// failure itself.
		Pump();
		Pump();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a GameObject without a guid is refused")
	{
		const std::string text =
			"NoGuid(" + Loom::Guid::New().ToString() + "):\n"
			"\tGameObject Root\n";

		const Loaded loaded(text);

		CHECK(loaded.scene == nullptr);
		CHECK(loaded.error.find("guid") != std::string::npos);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a component has to sit under a GameObject")
	{
		MarkerRegistered registered;

		const std::string text =
			"Loose(" + Loom::Guid::New().ToString() + "):\n"
			"\tComponent Marker(" + Loom::Guid::New().ToString() + ")\n";

		const Loaded loaded(text);

		CHECK(loaded.scene == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh keeps its geometry")
	{
		Loom::Scene scene("Geometry");
		Pump();

		Loom::Mesh* mesh = scene.GetRoot().Attach<Loom::Mesh>((uint32_t)4);
		Pump();

		mesh->m_vertices = { 0.0f, 0.5f, 0.0f, -0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f };

		const std::string text = Written(scene);

		const Loaded loaded(text);

		REQUIRE(loaded.scene != nullptr);

		Loom::Mesh* read = loaded.scene->GetRoot().GetComponent<Loom::Mesh>();

		REQUIRE(read != nullptr);
		CHECK(*read->m_vertices == *mesh->m_vertices);
		CHECK(read->primitive_id == 4);
	};
};
