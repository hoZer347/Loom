#include "doctest.h"

#include "Test Support.h"

#include "Collider.h"
#include "ComponentRegistry.h"
#include "Physics.h"
#include "Scene.h"

#include "glm/glm.hpp"

#include <cmath>
#include <random>
#include <type_traits>

using LoomTests::Pump;

static constexpr float EPSILON = 1e-4f;

static constexpr glm::vec3 FORWARD(0.0f, 0.0f, -1.0f);
static constexpr glm::vec3 DOWN(0.0f, -1.0f, 0.0f);

static void CheckNear(const glm::vec3& actual, const glm::vec3& expected)
{
	for (int axis = 0; axis < glm::vec3::length(); axis++)
		CHECK(actual[axis] == doctest::Approx(expected[axis]).epsilon(EPSILON));
};

// A child of the scene's root at position, carrying a T.
template <typename T>
static T* Place(Loom::Scene& scene, const glm::vec3& position, const std::string& name = "Shape")
{
	Loom::GameObject* object = scene.AddChild(name);
	Pump();

	object->transform.position = position;

	T* collider = object->Attach<T>();
	Pump();

	return collider;
};


TEST_SUITE("Collider")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "box and sphere colliders are components the editor can add by name, the base is not")
	{
		CHECK(std::is_base_of_v<Loom::Collider, Loom::BoxCollider>);
		CHECK(std::is_base_of_v<Loom::Collider, Loom::SphereCollider>);
		CHECK(std::is_abstract_v<Loom::Collider>);

		Loom::Scene scene("colliders by name");
		Pump();

		CHECK(Loom::ComponentRegistry::Create("Collider", scene.GetRoot()) == nullptr);

		Loom::ComponentBase* box = Loom::ComponentRegistry::Create("BoxCollider", scene.GetRoot());
		Loom::ComponentBase* sphere = Loom::ComponentRegistry::Create("SphereCollider", scene.GetRoot());
		Pump();

		CHECK(dynamic_cast<Loom::BoxCollider*>(box) != nullptr);
		CHECK(dynamic_cast<Loom::SphereCollider*>(sphere) != nullptr);
		CHECK(scene.GetRoot().GetComponent<Loom::BoxCollider>() == box);
		CHECK(scene.GetRoot().GetComponent<Loom::SphereCollider>() == sphere);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "GetComponent<Collider> finds whichever kind there is")
	{
		Loom::Scene scene("any collider");
		Pump();

		CHECK(scene.GetRoot().GetComponent<Loom::Collider>() == nullptr);

		Loom::SphereCollider* sphere = Place<Loom::SphereCollider>(scene, glm::vec3(0.0f));

		CHECK(scene.GetRoot().GetChildren()[0]->GetComponent<Loom::Collider>() == sphere);
		CHECK(scene.GetRoot().FindComponent<Loom::Collider>() == sphere);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a ray meets a box at its face, facing back at the ray")
	{
		Loom::Scene scene("box face");
		Pump();

		Loom::BoxCollider* box = Place<Loom::BoxCollider>(scene, glm::vec3(0.0f, 0.0f, -5.0f));
		box->size = glm::vec3(2.0f);

		Loom::RaycastHit hit;
		REQUIRE(box->Raycast({ glm::vec3(0.0f), FORWARD }, hit));

		CHECK(hit.distance == doctest::Approx(4.0f));
		CheckNear(hit.point, glm::vec3(0.0f, 0.0f, -4.0f));
		CheckNear(hit.normal, glm::vec3(0.0f, 0.0f, 1.0f));
		CHECK(hit.collider == box);
		CHECK(hit.gameObject == box->GetGameObject());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a box is missed beside it, past maxDistance, and from inside")
	{
		Loom::Scene scene("box misses");
		Pump();

		Loom::BoxCollider* box = Place<Loom::BoxCollider>(scene, glm::vec3(0.0f, 0.0f, -5.0f));

		Loom::RaycastHit hit;
		CHECK_FALSE(box->Raycast({ glm::vec3(2.0f, 0.0f, 0.0f), FORWARD }, hit));
		CHECK_FALSE(box->Raycast({ glm::vec3(0.0f), FORWARD }, hit, 4.0f));
		CHECK(box->Raycast({ glm::vec3(0.0f), FORWARD }, hit, 4.5f));
		CHECK_FALSE(box->Raycast({ glm::vec3(0.0f, 0.0f, -5.0f), FORWARD }, hit));
		CHECK_FALSE(box->Raycast({ glm::vec3(0.0f), -FORWARD }, hit));
		CHECK_FALSE(box->Raycast({ glm::vec3(0.0f), glm::vec3(0.0f) }, hit));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a box turns and stretches with its transform and its parents'")
	{
		Loom::Scene scene("turned box");
		Pump();

		Loom::GameObject* parent = scene.AddChild("Parent");
		Pump();
		parent->transform.position = glm::vec3(0.0f, 0.0f, -10.0f);
		parent->transform.scale = glm::vec3(2.0f, 1.0f, 1.0f);

		Loom::GameObject* child = parent->AddChild("Child");
		Pump();
		child->transform.rotation = glm::vec3(0.0f, 90.0f, 0.0f);

		Loom::BoxCollider* box = child->Attach<Loom::BoxCollider>();
		Pump();

		// The child's local Z lies along the parent's X, which is stretched
		// twice over, so the box reaches 1 either side along world X and its
		// face toward a ray from the side is at x = 1.
		Loom::RaycastHit hit;
		REQUIRE(box->Raycast({ glm::vec3(5.0f, 0.0f, -10.0f), glm::vec3(-1.0f, 0.0f, 0.0f) }, hit));

		CHECK(hit.distance == doctest::Approx(4.0f));
		CheckNear(hit.normal, glm::vec3(1.0f, 0.0f, 0.0f));

		const Loom::Bounds bounds = box->WorldBounds();
		CheckNear(bounds.min, glm::vec3(-1.0f, -0.5f, -10.5f));
		CheckNear(bounds.max, glm::vec3(1.0f, 0.5f, -9.5f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a wireframe outlines the collider where it is in the world")
	{
		constexpr size_t BOX_EDGES = 12;
		constexpr size_t SPHERE_CIRCLES = 3;
		constexpr size_t CIRCLE_SEGMENTS = 32;

		Loom::Scene scene("wireframes");
		Pump();

		Loom::BoxCollider* box = Place<Loom::BoxCollider>(scene, glm::vec3(3.0f, 0.0f, 0.0f));
		box->size = glm::vec3(2.0f, 4.0f, 6.0f);

		const std::vector<glm::vec3> edges = box->Wireframe();
		const Loom::Bounds bounds = box->WorldBounds();

		REQUIRE(edges.size() == BOX_EDGES * 2);

		for (size_t i = 0; i < edges.size(); i += 2)
		{
			// Each edge runs along one axis, from one face of the box to the other.
			const glm::vec3 along = glm::abs(edges[i + 1] - edges[i]);
			CHECK((along.x > 0.0f) + (along.y > 0.0f) + (along.z > 0.0f) == 1);

			CHECK(glm::all(glm::lessThanEqual(bounds.min, glm::min(edges[i], edges[i + 1]))));
			CHECK(glm::all(glm::lessThanEqual(glm::max(edges[i], edges[i + 1]), bounds.max)));
		};

		Loom::SphereCollider* sphere = Place<Loom::SphereCollider>(scene, glm::vec3(0.0f, 5.0f, 0.0f));
		const std::vector<glm::vec3> circles = sphere->Wireframe();

		REQUIRE(circles.size() == SPHERE_CIRCLES * CIRCLE_SEGMENTS * 2);

		for (const glm::vec3& point : circles)
			CHECK(glm::length(point - glm::vec3(0.0f, 5.0f, 0.0f)) == doctest::Approx(0.5f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a box flattened to nothing is never hit")
	{
		Loom::Scene scene("flat box");
		Pump();

		Loom::BoxCollider* box = Place<Loom::BoxCollider>(scene, glm::vec3(0.0f, 0.0f, -5.0f));
		scene.GetRoot().GetChildren()[0]->transform.scale = glm::vec3(1.0f, 0.0f, 1.0f);

		Loom::RaycastHit hit;
		CHECK_FALSE(box->Raycast({ glm::vec3(0.0f), FORWARD }, hit));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a sphere is hit on its surface and stays round under uneven scale")
	{
		Loom::Scene scene("sphere");
		Pump();

		Loom::SphereCollider* sphere = Place<Loom::SphereCollider>(scene, glm::vec3(0.0f, -10.0f, 0.0f));
		sphere->center = glm::vec3(0.0f, 1.0f, 0.0f);

		Loom::RaycastHit hit;
		REQUIRE(sphere->Raycast({ glm::vec3(0.0f), DOWN }, hit));

		CHECK(hit.distance == doctest::Approx(8.5f));
		CheckNear(hit.normal, glm::vec3(0.0f, 1.0f, 0.0f));

		scene.GetRoot().GetChildren()[0]->transform.scale = glm::vec3(1.0f, 1.0f, 4.0f);

		// Radius 2 now, by the largest scale, around the same centre.
		REQUIRE(sphere->Raycast({ glm::vec3(0.0f), DOWN }, hit));
		CHECK(hit.distance == doctest::Approx(7.0f));

		CHECK_FALSE(sphere->Raycast({ glm::vec3(0.0f, -9.0f, 0.0f), DOWN }, hit));
		CHECK_FALSE(sphere->Raycast({ glm::vec3(3.0f, 0.0f, 0.0f), DOWN }, hit));
	};
};


TEST_SUITE("ColliderTree")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene finds its colliders once a physics step has synced them")
	{
		Loom::Scene scene("synced");
		Pump();

		Place<Loom::BoxCollider>(scene, glm::vec3(0.0f, 0.0f, -5.0f));

		Loom::RaycastHit hit;
		CHECK_FALSE(scene.Raycast({ glm::vec3(0.0f), FORWARD }, hit));

		scene.Physics();

		CHECK(scene.GetColliders().Size() == 1);
		CHECK(scene.Raycast({ glm::vec3(0.0f), FORWARD }, hit));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Raycast answers the nearest hit and RaycastAll every hit, nearest first")
	{
		Loom::Scene scene("nearest");
		Pump();

		Loom::BoxCollider* far = Place<Loom::BoxCollider>(scene, glm::vec3(0.0f, 0.0f, -9.0f), "Far");
		Loom::SphereCollider* near = Place<Loom::SphereCollider>(scene, glm::vec3(0.0f, 0.0f, -3.0f), "Near");
		Place<Loom::BoxCollider>(scene, glm::vec3(4.0f, 0.0f, -3.0f), "Aside");

		scene.Physics();

		const Loom::Ray ray{ glm::vec3(0.0f), FORWARD * 3.0f };

		Loom::RaycastHit hit;
		REQUIRE(scene.Raycast(ray, hit));
		CHECK(hit.collider == near);
		CHECK(hit.distance == doctest::Approx(2.5f));

		const std::vector<Loom::RaycastHit> hits = scene.RaycastAll(ray);
		REQUIRE(hits.size() == 2);
		CHECK(hits[0].collider == near);
		CHECK(hits[1].collider == far);

		CHECK(scene.RaycastAll(ray, 5.0f).size() == 1);
		CHECK_FALSE(scene.Raycast(ray, hit, 2.0f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a collider is found where the last physics step left it")
	{
		Loom::Scene scene("moving");
		Pump();

		Loom::BoxCollider* box = Place<Loom::BoxCollider>(scene, glm::vec3(0.0f, 0.0f, -5.0f));
		Loom::GameObject* object = scene.GetRoot().GetChildren()[0];

		scene.Physics();

		const Loom::Bounds padded = *scene.GetColliders().PaddedBounds(*box);

		// Within the padding the leaf stays as it was.
		object->transform.position = glm::vec3(0.0f, 0.05f, -5.0f);
		scene.Physics();

		CheckNear(scene.GetColliders().PaddedBounds(*box)->min, padded.min);

		object->transform.position = glm::vec3(10.0f, 0.0f, -5.0f);

		Loom::RaycastHit hit;
		REQUIRE(scene.Raycast({ glm::vec3(0.0f), FORWARD }, hit));
		CHECK(hit.distance == doctest::Approx(4.5f));
		CHECK_FALSE(scene.Raycast({ glm::vec3(10.0f, 0.0f, 0.0f), FORWARD }, hit));

		// Asked directly, a collider answers from where it is now.
		CHECK(box->Raycast({ glm::vec3(10.0f, 0.0f, 0.0f), FORWARD }, hit));

		scene.Physics();

		CHECK_FALSE(scene.Raycast({ glm::vec3(0.0f), FORWARD }, hit));
		CHECK(scene.Raycast({ glm::vec3(10.0f, 0.0f, 0.0f), FORWARD }, hit));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a disabled, detached or destroyed collider drops out of the tree")
	{
		Loom::Scene scene("dropping out");
		Pump();

		Loom::BoxCollider* disabled = Place<Loom::BoxCollider>(scene, glm::vec3(0.0f, 0.0f, -5.0f), "Disabled");
		Loom::BoxCollider* detached = Place<Loom::BoxCollider>(scene, glm::vec3(5.0f, 0.0f, -5.0f), "Detached");
		Place<Loom::BoxCollider>(scene, glm::vec3(-5.0f, 0.0f, -5.0f), "Destroyed");

		std::vector<Loom::GameObject*> objects = scene.GetRoot().GetChildren();

		scene.Physics();
		REQUIRE(scene.GetColliders().Size() == 3);

		objects[0]->SetDisabled(true);
		scene.Physics();

		Loom::RaycastHit hit;
		CHECK(scene.GetColliders().Size() == 2);
		CHECK_FALSE(scene.Raycast({ glm::vec3(0.0f), FORWARD }, hit));

		objects[0]->SetDisabled(false);
		scene.Physics();
		CHECK(scene.Raycast({ glm::vec3(0.0f), FORWARD }, hit));

		objects[1]->DetachComponent(detached);
		Pump();
		CHECK(scene.GetColliders().Size() == 2);

		objects[2]->Destroy();
		Pump();
		Pump();
		CHECK(scene.GetColliders().Size() == 1);
		CHECK(scene.GetColliders().PaddedBounds(*disabled) != nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a collider moved to another scene moves to its tree")
	{
		Loom::Scene first("first");
		Loom::Scene second("second");
		Pump();

		Loom::BoxCollider* box = Place<Loom::BoxCollider>(first, glm::vec3(0.0f, 0.0f, -5.0f));
		first.Physics();

		first.GetRoot().GetChildren()[0]->SetParent(&second.GetRoot());
		Pump();

		second.Physics();
		CHECK(second.GetColliders().Size() == 1);
		CHECK(first.GetColliders().Size() == 0);

		first.Physics();
		CHECK(second.GetColliders().Size() == 1);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Physics::Raycast looks through every open scene")
	{
		Loom::Scene first("first");
		Loom::Scene second("second");
		Pump();

		Place<Loom::BoxCollider>(first, glm::vec3(0.0f, 0.0f, -9.0f));
		Loom::BoxCollider* nearer = Place<Loom::BoxCollider>(second, glm::vec3(0.0f, 0.0f, -5.0f));

		first.Physics();
		second.Physics();

		Loom::RaycastHit hit;
		REQUIRE(Loom::Physics::Raycast(glm::vec3(0.0f), FORWARD, hit));
		CHECK(hit.collider == nearer);

		CHECK(Loom::Physics::RaycastAll(glm::vec3(0.0f), FORWARD).size() == 2);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a collider on an object no thread processes is still synced")
	{
		Loom::Scene scene("no thread");
		Pump();

		Place<Loom::BoxCollider>(scene, glm::vec3(0.0f, 0.0f, -5.0f));
		scene.GetRoot().GetChildren()[0]->SetThreadID(-1);

		scene.Physics();

		Loom::RaycastHit hit;
		CHECK(scene.Raycast({ glm::vec3(0.0f), FORWARD }, hit));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the tree stays shallow and answers what testing every collider would, through moves and removals")
	{
		constexpr int COUNT = 300;
		constexpr int RAYS = 200;
		constexpr int ROUNDS = 6;
		constexpr unsigned SEED = 7;
		constexpr float SPREAD = 20.0f;

		// Moved farther than any padding, so a moved leaf is always reinserted.
		constexpr float JUMP = 3.0f;

		// A balanced tree of COUNT leaves is about log2(COUNT) deep; this
		// allows the rotations some slack and still fails a tree that degrades
		// into a list.
		constexpr int MOST_HEIGHT = 20;

		Loom::Scene scene("many");
		Pump();

		std::mt19937 random(SEED);
		std::uniform_real_distribution<float> place(-SPREAD, SPREAD);
		std::uniform_real_distribution<float> extent(0.2f, 2.0f);
		std::uniform_real_distribution<float> angle(0.0f, 360.0f);
		std::uniform_real_distribution<float> jump(-JUMP, JUMP);
		enum Fate { MOVE, DISABLE, ENABLE, KEEP, FATES };
		std::uniform_int_distribution<int> fate(0, FATES - 1);

		std::vector<Loom::Collider*> colliders{ };
		std::vector<Loom::GameObject*> objects{ };

		for (int i = 0; i < COUNT; i++)
		{
			const glm::vec3 position(place(random), place(random), place(random));

			if (i % 2 == 0)
			{
				Loom::BoxCollider* box = Place<Loom::BoxCollider>(scene, position);
				box->size = glm::vec3(extent(random), extent(random), extent(random));
				colliders.push_back(box);
			}
			else
			{
				Loom::SphereCollider* sphere = Place<Loom::SphereCollider>(scene, position);
				sphere->radius = extent(random);
				colliders.push_back(sphere);
			};

			objects.push_back(scene.GetRoot().GetChildren().back());
			objects.back()->transform.rotation = glm::vec3(angle(random), angle(random), angle(random));
		};

		for (int round = 0; round < ROUNDS; round++)
		{
			// The first round builds the tree; every later one moves a quarter
			// of the colliders, disables a quarter and enables a quarter.
			if (round > 0)
				for (Loom::GameObject* object : objects)
					switch (fate(random))
					{
					case MOVE:
						object->transform.position = *object->transform.position + glm::vec3(jump(random), jump(random), jump(random));
						break;
					case DISABLE:
						object->SetDisabled(true);
						break;
					case ENABLE:
						object->SetDisabled(false);
						break;
					default:
						break;
					};

			scene.Physics();

			std::vector<Loom::Collider*> enabled{ };

			for (size_t i = 0; i < objects.size(); i++)
				if (!objects[i]->IsDisabled())
					enabled.push_back(colliders[i]);

			CHECK(scene.GetColliders().Size() == enabled.size());
			CHECK(scene.GetColliders().Height() <= MOST_HEIGHT);

			int agreed = 0;

			for (int i = 0; i < RAYS; i++)
			{
				const Loom::Ray ray
				{
					glm::vec3(place(random), place(random), place(random)),
					glm::vec3(place(random), place(random), place(random))
				};

				Loom::RaycastHit nearest;
				size_t hits = 0;

				for (Loom::Collider* collider : enabled)
				{
					Loom::RaycastHit hit;

					if (collider->Raycast(ray, hit) && (hits++ == 0 || hit.distance < nearest.distance))
						nearest = hit;
				};

				Loom::RaycastHit found;
				const bool hit = scene.Raycast(ray, found);

				if (hit == (hits > 0)
					&& (!hit || found.collider == nearest.collider)
					&& scene.RaycastAll(ray).size() == hits)
					agreed++;
			};

			CHECK(agreed == RAYS);
		};
	};
};
