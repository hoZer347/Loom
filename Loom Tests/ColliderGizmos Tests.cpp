#include "doctest.h"

#include "Test Support.h"

#include "Camera.h"
#include "Collider.h"
#include "ColliderGizmos.h"
#include "Scene.h"

using LoomTests::Pump;
using LoomTests::RecordingRenderer;


namespace
{
	constexpr const char* GIZMOS = "Loom Editor Collider Gizmos";
};


TEST_SUITE("ColliderGizmos")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the colliders under a selection are drawn in one go over the view")
	{
		RecordingRenderer renderer;

		Loom::Scene scene("collider gizmos");
		Pump();

		Loom::Camera* camera = scene.Attach<Loom::Camera>();
		Loom::GameObject* parent = scene.AddChild("Parent");
		Pump();

		Loom::GameObject* child = parent->AddChild("Child");
		Pump();

		child->Attach<Loom::BoxCollider>();
		Pump();

		Loom::ColliderGizmos gizmos;
		gizmos.Draw(*parent, *camera);

		CHECK(renderer.drawn == std::vector<uint32_t>{ renderer.programs.at(GIZMOS) });
		CHECK(renderer.drawnInto == std::vector<uint32_t>{ RecordingRenderer::WINDOW });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "nothing is drawn, or compiled, for a selection without an enabled collider")
	{
		RecordingRenderer renderer;

		Loom::Scene scene("no collider gizmos");
		Pump();

		Loom::Camera* camera = scene.Attach<Loom::Camera>();
		Loom::GameObject* disabled = scene.AddChild("Disabled");
		Pump();

		disabled->Attach<Loom::SphereCollider>();
		disabled->SetDisabled(true);
		Pump();

		Loom::ColliderGizmos gizmos;
		gizmos.Draw(scene.GetRoot(), *camera);

		CHECK(renderer.drawn.empty());
		CHECK(renderer.programs.count(GIZMOS) == 0);
	};
};
