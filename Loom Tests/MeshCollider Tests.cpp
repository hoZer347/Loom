#include "doctest.h"

#include "Test Support.h"

#include "ComponentRegistry.h"
#include "Mesh.h"
#include "MeshCollider.h"
#include "Physics.h"
#include "Scene.h"

#include "glm/glm.hpp"

#include <random>
#include <vector>

using LoomTests::Pump;
using LoomTests::EPSILON;

#pragma region Helpers

static constexpr glm::vec3 FORWARD(0.0f, 0.0f, -1.0f);

static constexpr size_t COMPONENTS = 3;
static constexpr size_t CORNERS = 3;
static constexpr size_t Z_COMPONENT = 2;

// Two triangles making a square two across, facing along Z.
static const std::vector<float> SQUARE
{
	-1.0f, -1.0f, 0.0f,   1.0f, -1.0f, 0.0f,   1.0f, 1.0f, 0.0f,
	-1.0f, -1.0f, 0.0f,   1.0f,  1.0f, 0.0f,  -1.0f, 1.0f, 0.0f,
};

static void CheckNear(const glm::vec3& actual, const glm::vec3& expected)
{
	for (int axis = 0; axis < glm::vec3::length(); axis++)
		CHECK(actual[axis] == doctest::Approx(expected[axis]).epsilon(EPSILON));
};

// A child of the scene's root at position with a Mesh of vertices drawn as
// primitive, and a MeshCollider over it.
static Loom::MeshCollider* PlaceMesh(
	Loom::Scene& scene,
	const glm::vec3& position,
	const std::vector<float>& vertices,
	Loom::Mesh::Primitive primitive = Loom::Mesh::Triangles)
{
	Loom::GameObject* object = scene.AddChild("Mesh");
	Pump();

	object->transform.position = position;

	Loom::Mesh* mesh = object->Attach<Loom::Mesh>(primitive);
	mesh->m_vertices = vertices;

	Loom::MeshCollider* collider = object->Attach<Loom::MeshCollider>();
	Pump();

	return collider;
};

#pragma endregion


TEST_SUITE("MeshCollider")
{
	#pragma region Raycasts

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh collider is a collider the editor can add by name")
	{
		Loom::Scene scene("mesh collider by name");
		Pump();

		Loom::ComponentBase* created = Loom::ComponentRegistry::Create("MeshCollider", scene.GetRoot());
		Pump();

		CHECK(dynamic_cast<Loom::MeshCollider*>(created) != nullptr);
		CHECK(scene.GetRoot().GetComponent<Loom::Collider>() == created);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a ray meets the mesh's triangles from either side, the normal facing back at it")
	{
		Loom::Scene scene("square");
		Pump();

		Loom::MeshCollider* collider = PlaceMesh(scene, glm::vec3(0.0f, 0.0f, -5.0f), SQUARE);
		CHECK(collider->TriangleCount() == 0);

		Loom::RaycastHit hit;
		REQUIRE(collider->Raycast({ glm::vec3(0.5f, 0.5f, 0.0f), FORWARD }, hit));

		CHECK(collider->TriangleCount() == 2);
		CHECK(hit.distance == doctest::Approx(5.0f));
		CheckNear(hit.point, glm::vec3(0.5f, 0.5f, -5.0f));
		CheckNear(hit.normal, glm::vec3(0.0f, 0.0f, 1.0f));
		CHECK(hit.collider == collider);

		REQUIRE(collider->Raycast({ glm::vec3(-0.5f, 0.0f, -10.0f), -FORWARD }, hit));
		CHECK(hit.distance == doctest::Approx(5.0f));
		CheckNear(hit.normal, glm::vec3(0.0f, 0.0f, -1.0f));

		CHECK_FALSE(collider->Raycast({ glm::vec3(1.5f, 0.0f, 0.0f), FORWARD }, hit));
		CHECK_FALSE(collider->Raycast({ glm::vec3(0.0f), FORWARD }, hit, 4.0f));
		CHECK_FALSE(collider->Raycast({ glm::vec3(0.0f), -FORWARD }, hit));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the mesh turns and stretches with its transform")
	{
		Loom::Scene scene("turned square");
		Pump();

		Loom::MeshCollider* collider = PlaceMesh(scene, glm::vec3(0.0f, 0.0f, -5.0f), SQUARE);
		Loom::GameObject* object = scene.GetRoot().GetChildren()[0];

		// Facing along X now, three across.
		object->transform.rotation = glm::vec3(0.0f, 90.0f, 0.0f);
		object->transform.scale = glm::vec3(1.5f);

		Loom::RaycastHit hit;
		REQUIRE(collider->Raycast({ glm::vec3(4.0f, 1.2f, -6.2f), glm::vec3(-1.0f, 0.0f, 0.0f) }, hit));

		CHECK(hit.distance == doctest::Approx(4.0f));
		CheckNear(hit.normal, glm::vec3(1.0f, 0.0f, 0.0f));

		const Loom::Bounds bounds = collider->WorldBounds();
		CheckNear(bounds.min, glm::vec3(0.0f, -1.5f, -6.5f));
		CheckNear(bounds.max, glm::vec3(0.0f, 1.5f, -3.5f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "strips and fans are triangles too, and points and lines have none")
	{
		Loom::Scene scene("primitives");
		Pump();

		const std::vector<float> corners
		{
			-1.0f, -1.0f, 0.0f,   1.0f, -1.0f, 0.0f,   -1.0f, 1.0f, 0.0f,   1.0f, 1.0f, 0.0f,
		};

		Loom::MeshCollider* strip = PlaceMesh(scene, glm::vec3(0.0f), corners, Loom::Mesh::TriangleStrip);
		Loom::MeshCollider* fan = PlaceMesh(scene, glm::vec3(0.0f), corners, Loom::Mesh::TriangleFan);
		Loom::MeshCollider* lines = PlaceMesh(scene, glm::vec3(0.0f), corners, Loom::Mesh::Lines);

		Loom::RaycastHit hit;
		CHECK(strip->Raycast({ glm::vec3(0.5f, 0.5f, 5.0f), FORWARD }, hit));
		CHECK(strip->TriangleCount() == 2);

		fan->WorldBounds();
		CHECK(fan->TriangleCount() == 2);

		CHECK_FALSE(lines->Raycast({ glm::vec3(0.5f, 0.5f, 5.0f), FORWARD }, hit));
		CHECK(lines->TriangleCount() == 0);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a GameObject with no Mesh has nothing to hit")
	{
		Loom::Scene scene("no mesh");
		Pump();

		Loom::GameObject* object = scene.AddChild("Empty");
		Pump();

		object->transform.position = glm::vec3(1.0f, 2.0f, 3.0f);

		Loom::MeshCollider* collider = object->Attach<Loom::MeshCollider>();
		Pump();

		Loom::RaycastHit hit;
		CHECK_FALSE(collider->Raycast({ glm::vec3(1.0f, 2.0f, 10.0f), FORWARD }, hit));
		CHECK(collider->Wireframe().empty());

		const Loom::Bounds bounds = collider->WorldBounds();
		CheckNear(bounds.min, glm::vec3(1.0f, 2.0f, 3.0f));
		CheckNear(bounds.max, glm::vec3(1.0f, 2.0f, 3.0f));

		scene.Physics();
		CHECK_FALSE(scene.Raycast({ glm::vec3(1.0f, 2.0f, 10.0f), FORWARD }, hit));
	};

	#pragma endregion

	#pragma region Editing

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "editing the mesh's vertices moves what the scene's raycasts hit at the next step")
	{
		Loom::Scene scene("edited mesh");
		Pump();

		PlaceMesh(scene, glm::vec3(0.0f, 0.0f, -5.0f), SQUARE);
		Loom::Mesh* mesh = scene.GetRoot().GetChildren()[0]->GetComponent<Loom::Mesh>();

		scene.Physics();

		Loom::RaycastHit hit;
		REQUIRE(scene.Raycast({ glm::vec3(0.0f), FORWARD }, hit));
		CHECK(hit.distance == doctest::Approx(5.0f));

		// Pushed two back, in place, the way a script rebuilds a mesh.
		for (size_t z = Z_COMPONENT; z < mesh->m_vertices->size(); z += COMPONENTS)
			(*mesh->m_vertices)[z] = -2.0f;

		scene.Physics();

		REQUIRE(scene.Raycast({ glm::vec3(0.0f), FORWARD }, hit));
		CHECK(hit.distance == doctest::Approx(7.0f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an indexed mesh's triangles go through its indices")
	{
		Loom::Scene scene("indexed");
		Pump();

		// The square's four corners once each, with an index past them that
		// leaves its triangle out.
		const std::vector<float> corners
		{
			-1.0f, -1.0f, 0.0f,   1.0f, -1.0f, 0.0f,   1.0f, 1.0f, 0.0f,   -1.0f, 1.0f, 0.0f,
		};
		constexpr uint32_t PAST = 4;

		Loom::MeshCollider* collider = PlaceMesh(scene, glm::vec3(0.0f, 0.0f, -5.0f), corners);
		Loom::Mesh* mesh = scene.GetRoot().GetChildren()[0]->GetComponent<Loom::Mesh>();

		mesh->m_indices = { 0, 1, 2, 0, 2, 3, 0, 1, PAST };

		Loom::RaycastHit hit;
		REQUIRE(collider->Raycast({ glm::vec3(-0.5f, 0.5f, 0.0f), FORWARD }, hit));
		CHECK(hit.distance == doctest::Approx(5.0f));
		CHECK(collider->TriangleCount() == 2);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh edited and asked directly between steps is refitted in the scene's tree")
	{
		Loom::Scene scene("refitted");
		Pump();

		Loom::MeshCollider* collider = PlaceMesh(scene, glm::vec3(0.0f, 0.0f, -5.0f), SQUARE);
		Loom::Mesh* mesh = scene.GetRoot().GetChildren()[0]->GetComponent<Loom::Mesh>();

		scene.Physics();

		// Moved well outside the leaf it was synced into.
		constexpr float AWAY = 10.0f;

		for (size_t x = 0; x < mesh->m_vertices->size(); x += COMPONENTS)
			(*mesh->m_vertices)[x] += AWAY;

		Loom::RaycastHit hit;
		REQUIRE(collider->Raycast({ glm::vec3(AWAY, 0.0f, 0.0f), FORWARD }, hit));
		CHECK(scene.Raycast({ glm::vec3(AWAY, 0.0f, 0.0f), FORWARD }, hit));
	};

	#pragma endregion

	#pragma region Wireframes and Size

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "there is no wireframe for the editor to draw, however many triangles there are")
	{
		Loom::Scene scene("mesh wireframe");
		Pump();

		Loom::MeshCollider* collider = PlaceMesh(scene, glm::vec3(0.0f, 0.0f, -5.0f), SQUARE);

		constexpr size_t TRIANGLES = 2;

		CHECK(collider->Wireframe().empty());
		CHECK(collider->TriangleCount() == TRIANGLES);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh of thousands of triangles answers what testing every one would")
	{
		constexpr int TRIANGLES = 3000;
		constexpr int RAYS = 300;
		constexpr unsigned SEED = 11;
		constexpr float SPREAD = 10.0f;
		constexpr float SIZE = 0.8f;

		Loom::Scene scene("many triangles");
		Pump();

		std::mt19937 random(SEED);
		std::uniform_real_distribution<float> place(-SPREAD, SPREAD);
		std::uniform_real_distribution<float> corner(-SIZE, SIZE);

		std::vector<float> vertices{ };
		std::vector<glm::vec3> corners{ };

		for (int i = 0; i < TRIANGLES; i++)
		{
			const glm::vec3 centre(place(random), place(random), place(random));

			for (size_t c = 0; c < CORNERS; c++)
			{
				const glm::vec3 point = centre + glm::vec3(corner(random), corner(random), corner(random));
				vertices.insert(vertices.end(), { point.x, point.y, point.z });
				corners.push_back(point);
			};
		};

		Loom::MeshCollider* collider = PlaceMesh(scene, glm::vec3(0.0f), vertices);

		int agreed = 0;
		int hits = 0;

		for (int i = 0; i < RAYS; i++)
		{
			const glm::vec3 origin(place(random), place(random), place(random));
			const glm::vec3 direction = glm::normalize(glm::vec3(place(random), place(random), place(random)));

			// Every triangle by brute force, by other maths than the collider's:
			// the plane, then inside all three edges.
			float nearest = Loom::INFINITE_DISTANCE;

			for (size_t t = 0; t < corners.size(); t += CORNERS)
			{
				const glm::vec3 normal = glm::cross(corners[t + 1] - corners[t], corners[t + 2] - corners[t]);
				const float facing = glm::dot(normal, direction);

				if (facing == 0.0f)
					continue;

				const float distance = glm::dot(normal, corners[t] - origin) / facing;

				if (distance < 0.0f || distance >= nearest)
					continue;

				const glm::vec3 point = origin + direction * distance;
				bool inside = true;

				for (size_t e = 0; e < CORNERS; e++)
				{
					const glm::vec3& from = corners[t + e];
					const glm::vec3& to = corners[t + (e + 1) % CORNERS];

					if (glm::dot(glm::cross(to - from, point - from), normal) < 0.0f)
						inside = false;
				};

				if (inside)
					nearest = distance;
			};

			if (nearest != Loom::INFINITE_DISTANCE)
				hits++;

			Loom::RaycastHit hit;
			const bool found = collider->Raycast({ origin, direction }, hit);

			if (found == (nearest != Loom::INFINITE_DISTANCE)
				&& (!found || hit.distance == doctest::Approx(nearest).epsilon(EPSILON)))
				agreed++;
		};

		CHECK(hits > 0);
		CHECK(agreed == RAYS);
	};

	#pragma endregion
};
