#include "doctest.h"

#include "Test Support.h"

#include "Collider.h"
#include "Mesh.h"
#include "MeshCollider.h"
#include "Physics.h"
#include "Rigidbody.h"
#include "Scene.h"

#include "glm/glm.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <random>
#include <sstream>
#include <vector>

using LoomTests::Pump;


namespace
{
	// Room each collider gets on average, so a scene of any size is as
	// crowded as any other and a ray meets about as many colliders in each.
	constexpr float SPACE_PER_COLLIDER = 8.0f;

	// One collider in this many is a MeshCollider over a cube, the rest boxes
	// and spheres in turn.
	constexpr int MESH_EVERY = 10;

	// How big a collider is along each axis, from smallest to largest.
	constexpr float LEAST_EXTENT = 0.3f;
	constexpr float MOST_EXTENT = 1.5f;

	constexpr float FULL_TURN = 360.0f;
	constexpr float HALF = 0.5f;

	constexpr double MICROSECONDS_PER_MILLISECOND = 1000.0;

	// A batch is timed this many times and the quickest kept, so a thread
	// switched out mid-batch costs a retry rather than the result.
	constexpr int TRIES = 5;

	enum Kind { BOX, SPHERE, KINDS };

	// A unit cube as twelve triangles, for the mesh colliders.
	const std::vector<float> CUBE = []()
	{
		constexpr int FACES = 6;
		constexpr int FACES_PER_AXIS = 2;

		std::vector<float> vertices{ };

		for (int face = 0; face < FACES; face++)
		{
			const int axis = face / FACES_PER_AXIS;

			glm::vec3 out(0.0f), across(0.0f), up(0.0f);
			out[axis] = face % FACES_PER_AXIS == 0 ? HALF : -HALF;
			across[(axis + 1) % glm::vec3::length()] = HALF;
			up[(axis + 2) % glm::vec3::length()] = HALF;

			const glm::vec3 corners[] =
			{
				out - across - up, out + across - up, out + across + up,
				out - across - up, out + across + up, out - across + up,
			};

			for (const glm::vec3& corner : corners)
				vertices.insert(vertices.end(), { corner.x, corner.y, corner.z });
		};

		return vertices;
	}();

	double MillisecondsOf(const std::function<void()>& work)
	{
		const auto start = std::chrono::steady_clock::now();
		work();

		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	};

	// A scene of count colliders scattered through a cube sized to hold them,
	// turned every which way.
	struct Crowd
	{
		Crowd(int count, unsigned seed) :
			scene("crowd"),
			random(seed),
			half(HALF * std::cbrt(count * SPACE_PER_COLLIDER))
		{
			Pump();

			std::uniform_real_distribution<float> extent(LEAST_EXTENT, MOST_EXTENT);
			std::uniform_real_distribution<float> angle(0.0f, FULL_TURN);

			for (int i = 0; i < count; i++)
			{
				Loom::GameObject* object = scene.AddChild("Collider");
				Pump();

				object->transform.position = Place();
				object->transform.rotation = glm::vec3(angle(random), angle(random), angle(random));

				if (i % MESH_EVERY == 0)
				{
					object->transform.scale = glm::vec3(extent(random), extent(random), extent(random));
					object->Attach<Loom::Mesh>()->m_vertices = CUBE;
					colliders.push_back(object->Attach<Loom::MeshCollider>());
				}
				else if (i % KINDS == BOX)
				{
					Loom::BoxCollider* box = object->Attach<Loom::BoxCollider>();
					box->size = glm::vec3(extent(random), extent(random), extent(random));
					colliders.push_back(box);
				}
				else
				{
					Loom::SphereCollider* sphere = object->Attach<Loom::SphereCollider>();
					sphere->radius = extent(random) * HALF;
					colliders.push_back(sphere);
				};

				objects.push_back(object);
			};

			Pump();
		};

		glm::vec3 Place()
		{
			std::uniform_real_distribution<float> along(-half, half);

			return glm::vec3(along(random), along(random), along(random));
		};

		std::vector<Loom::Ray> Rays(int count)
		{
			std::uniform_real_distribution<float> way(-1.0f, 1.0f);
			std::vector<Loom::Ray> rays{ };

			for (int i = 0; i < count; i++)
				rays.push_back({ Place(), glm::normalize(glm::vec3(way(random), way(random), way(random))) });

			return rays;
		};

		// The nearest hit by testing every collider, which the tree has to match.
		Loom::Collider* Nearest(const Loom::Ray& ray) const
		{
			Loom::Collider* nearest = nullptr;
			float distance = Loom::INFINITE_DISTANCE;

			for (Loom::Collider* collider : colliders)
			{
				Loom::RaycastHit hit;

				if (collider->Raycast(ray, hit, distance))
				{
					nearest = collider;
					distance = hit.distance;
				};
			};

			return nearest;
		};

		Loom::Scene scene;
		std::mt19937 random;
		float half;

		std::vector<Loom::GameObject*> objects{ };
		std::vector<Loom::Collider*> colliders{ };
	};

	// Milliseconds a ray, over the quickest of TRIES batches of them.
	double RaycastCost(const Loom::Scene& scene, const std::vector<Loom::Ray>& rays)
	{
		double quickest = std::numeric_limits<double>::infinity();

		for (int i = 0; i < TRIES; i++)
			quickest = std::min(quickest, MillisecondsOf(
				[&]()
				{
					Loom::RaycastHit hit;

					for (const Loom::Ray& ray : rays)
						scene.Raycast(ray, hit);
				}));

		return quickest / rays.size();
	};
};


// Timed against the clock and long under node, so they run once a day from daily-performance.ps1
// rather than with every change.
TEST_SUITE("Performance" * doctest::skip())
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene of twenty thousand colliders answers a raycast far faster than testing every one")
	{
		constexpr int COUNT = 20000;
		constexpr int RAYS = 1000;
		// Few, as each tests every collider; agreement is covered at length in
		// the ColliderTree suite.
		constexpr int CHECKED_RAYS = 3;
		constexpr unsigned SEED = 3;

		// One collider in this many is moved before the second timed step.
		constexpr int MOVED_EVERY = 10;

		// Far short of what the tree gives, so a slow machine still passes; a
		// tree that has degraded into testing most colliders does not.
		constexpr double LEAST_SPEEDUP = 20.0;

		Crowd crowd(COUNT, SEED);

		const double built = MillisecondsOf([&]() { crowd.scene.Physics(); });
		const double still = MillisecondsOf([&]() { crowd.scene.Physics(); });

		for (size_t i = 0; i < crowd.objects.size(); i += MOVED_EVERY)
			crowd.objects[i]->transform.position = crowd.Place();

		const double moved = MillisecondsOf([&]() { crowd.scene.Physics(); });

		REQUIRE(crowd.scene.GetColliders().Size() == COUNT);

		const double tree = RaycastCost(crowd.scene, crowd.Rays(RAYS));

		const std::vector<Loom::Ray> checked = crowd.Rays(CHECKED_RAYS);

		int agreed = 0;
		int hits = 0;

		const double every = MillisecondsOf(
			[&]()
			{
				for (const Loom::Ray& ray : checked)
				{
					Loom::Collider* nearest = crowd.Nearest(ray);

					Loom::RaycastHit hit;
					const bool found = crowd.scene.Raycast(ray, hit);

					hits += nearest != nullptr;
					agreed += found ? hit.collider == nearest : nearest == nullptr;
				};
			}) / CHECKED_RAYS;

		std::ostringstream report;
		report << COUNT << " colliders, tree height " << crowd.scene.GetColliders().Height() << ":"
			<< "\n  first physics step " << built << " ms"
			<< "\n  physics step, nothing moved " << still << " ms"
			<< "\n  physics step, one in " << MOVED_EVERY << " moved " << moved << " ms"
			<< "\n  raycast through the tree " << tree * MICROSECONDS_PER_MILLISECOND << " us"
			<< "\n  raycast testing every collider " << every * MICROSECONDS_PER_MILLISECOND << " us"
			<< "\n  " << every / tree << "x faster";
		MESSAGE(report.str());

		CHECK(hits > 0);
		CHECK(agreed == CHECKED_RAYS);
		CHECK(every / tree >= LEAST_SPEEDUP);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a raycast costs about the same among sixteen times the colliders")
	{
		constexpr int FEW = 1000;
		constexpr int GROWTH = 16;
		constexpr int MANY = FEW * GROWTH;
		constexpr int RAYS = 1000;
		constexpr unsigned SEED = 5;

		// Sixteen times the colliders adds four levels to a balanced tree, so
		// a ray's cost should grow by a small amount. Testing every collider
		// would make it sixteen times; this allows plenty of noise between the
		// two.
		constexpr double MOST_GROWTH = 6.0;

		double few = 0.0;
		double many = 0.0;

		{
			Crowd crowd(FEW, SEED);
			crowd.scene.Physics();
			few = RaycastCost(crowd.scene, crowd.Rays(RAYS));
		};

		{
			Crowd crowd(MANY, SEED);
			crowd.scene.Physics();
			many = RaycastCost(crowd.scene, crowd.Rays(RAYS));
		};

		std::ostringstream report;
		report << "raycast among " << FEW << " colliders " << few * MICROSECONDS_PER_MILLISECOND << " us, among "
			<< MANY << " " << many * MICROSECONDS_PER_MILLISECOND << " us: " << many / few << "x";
		MESSAGE(report.str());

		CHECK(many / few <= MOST_GROWTH);
	};
};


TEST_SUITE("PhysicsPiles")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "bodies dropped into a pit pile up inside it and fall asleep")
	{
		constexpr int COUNT = 200;
		constexpr float SECONDS = 5.0f;
		constexpr unsigned SEED = 9;

		// The pit: a floor whose top is at y = 0 and four walls around it.
		constexpr float INSIDE = 6.0f;
		constexpr float WALL = 2.0f;
		constexpr float WALL_HEIGHT = 10.0f;

		// The drop: a layer at a time, a body to each cell of a grid.
		constexpr float SPACING = 1.6f;
		constexpr float FIRST_LAYER = 2.0f;
		constexpr int PER_ROW = 6;
		constexpr float JITTER = 0.2f;

		// Small enough that, turned any way, it still fits its cell.
		constexpr float LARGEST = 0.9f;

		// How many may still be stirring by the end: balls still rolling to a
		// stop, and what they wake as they leave it. A pile that never
		// settles keeps them all awake.
		constexpr int MOST_AWAKE = COUNT / 2;

		// Nothing slows a ball rolling on a floor, here as in Unity, so a
		// scene that wants its balls to stop gives them angular drag.
		constexpr float ROLLING_DRAG = 1.0f;

		Loom::Scene scene("pit");
		Pump();

		const auto wall =
			[&](const glm::vec3& position, const glm::vec3& size)
			{
				Loom::GameObject* object = scene.AddChild("Wall");
				Pump();

				object->transform.position = position;
				object->Attach<Loom::BoxCollider>()->size = size;
			};

		const float outer = INSIDE + WALL * HALF;
		const float across = (INSIDE + WALL) / HALF;

		wall(glm::vec3(0.0f, -WALL * HALF, 0.0f), glm::vec3(across, WALL, across));
		wall(glm::vec3(outer, WALL_HEIGHT * HALF, 0.0f), glm::vec3(WALL, WALL_HEIGHT, across));
		wall(glm::vec3(-outer, WALL_HEIGHT * HALF, 0.0f), glm::vec3(WALL, WALL_HEIGHT, across));
		wall(glm::vec3(0.0f, WALL_HEIGHT * HALF, outer), glm::vec3(across, WALL_HEIGHT, WALL));
		wall(glm::vec3(0.0f, WALL_HEIGHT * HALF, -outer), glm::vec3(across, WALL_HEIGHT, WALL));

		std::mt19937 random(SEED);
		std::uniform_real_distribution<float> jitter(-JITTER, JITTER);
		std::uniform_real_distribution<float> extent(LEAST_EXTENT, LARGEST);
		std::uniform_real_distribution<float> angle(0.0f, FULL_TURN);

		std::vector<Loom::Rigidbody*> bodies{ };

		for (int i = 0; i < COUNT; i++)
		{
			const int layer = i / (PER_ROW * PER_ROW);
			const int cell = i % (PER_ROW * PER_ROW);

			Loom::GameObject* object = scene.AddChild("Body");
			Pump();

			object->transform.position = glm::vec3(
				(cell % PER_ROW - (PER_ROW - 1) * HALF) * SPACING + jitter(random),
				FIRST_LAYER + layer * SPACING,
				(cell / PER_ROW - (PER_ROW - 1) * HALF) * SPACING + jitter(random));
			object->transform.rotation = glm::vec3(angle(random), angle(random), angle(random));

			if (i % KINDS == BOX)
				object->Attach<Loom::BoxCollider>()->size = glm::vec3(extent(random), extent(random), extent(random));
			else
				object->Attach<Loom::SphereCollider>()->radius = extent(random) * HALF;

			Loom::Rigidbody* body = object->Attach<Loom::Rigidbody>();
			body->angularDrag = ROLLING_DRAG;
			bodies.push_back(body);
		};

		Pump();

		const int steps = (int)std::round(SECONDS / Loom::Physics::FixedDeltaTime());
		double busiest = 0.0;
		double last = 0.0;

		for (int step = 0; step < steps; step++)
		{
			last = MillisecondsOf([&]() { scene.Physics(); });
			busiest = std::max(busiest, last);
		};

		int awake = 0;
		int inside = 0;

		for (Loom::Rigidbody* body : bodies)
		{
			const glm::vec3 position(body->GetGameObject()->WorldMatrix()[3]);

			awake += !body->IsSleeping();
			inside += position.y > 0.0f && std::abs(position.x) < INSIDE && std::abs(position.z) < INSIDE;
		};

		std::ostringstream report;
		report << COUNT << " bodies dropped into a pit, after " << SECONDS << " s:"
			<< "\n  busiest physics step " << busiest << " ms"
			<< "\n  last physics step " << last << " ms"
			<< "\n  " << awake << " still awake, " << inside << " inside the pit";
		MESSAGE(report.str());

		CHECK(inside == COUNT);
		CHECK(awake <= MOST_AWAKE);
	};
};
