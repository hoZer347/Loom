#include "doctest.h"

#include "Test Support.h"

#include "Collider.h"
#include "Mesh.h"
#include "MeshCollider.h"
#include "Physics.h"
#include "Rigidbody.h"
#include "Scene.h"

#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using LoomTests::Pump;


namespace
{
	// The Raycasts demo's Crowd scene, as Benchmarks/Unity Crowd builds it in
	// Unity, so the two can be timed against each other; every number here
	// matches one there, and so does every body, drawn from the same
	// generator in the same order. run-physics-benchmark.ps1 runs both.
	constexpr int COUNT = 1500;
	constexpr int STEPS = 1200;
	constexpr uint32_t SEED = 1;

	constexpr float HALF = 0.5f;
	constexpr float FULL_TURN = 360.0f;

	constexpr float LEAST_SIZE = 0.4f;
	constexpr float MOST_SIZE = 1.4f;
	// Room for the largest body, turned any way, in each cell of the drop.
	constexpr float ROOM = 1.8f;
	constexpr float SPACING = MOST_SIZE * ROOM;
	constexpr float SPREAD = 10.0f;
	constexpr float ABOVE = 12.0f;
	constexpr float DRAG = 0.5f;
	constexpr int KINDS = 2;

	// The pit: a floor and four walls around a square INSIDE either side of
	// the middle, its floor's top at y = 0.
	constexpr float INSIDE = 12.0f;
	constexpr float WALL = 4.0f;
	constexpr float WALL_HEIGHT = 14.0f;

	struct Placed
	{
		glm::vec3 position;
		glm::vec3 rotation;
	};

	// The demo scene's three rings.
	constexpr Placed RINGS[] =
	{
		{ { -4.0f, 5.0f, -3.0f }, { 70.0f, 20.0f, 0.0f } },
		{ { 4.0f, 4.0f, 3.0f }, { 75.0f, -30.0f, 0.0f } },
		{ { 0.0f, 8.0f, 1.0f }, { 60.0f, 90.0f, 0.0f } },
	};

	constexpr int RING_SEGMENTS = 48;
	constexpr int TUBE_SEGMENTS = 16;
	constexpr float RING_RADIUS = 0.35f;
	constexpr float TUBE_RADIUS = 0.15f;
	constexpr float RING_SCALE = 7.0f;

	constexpr int COLUMNS = 160;
	constexpr int ROWS = 90;
	constexpr float FIELD_OF_VIEW = 45.0f;
	constexpr float ASPECT = 16.0f / 9.0f;
	constexpr glm::vec3 EYE(0.0f, 42.0f, 34.0f);
	constexpr glm::vec3 TARGET(0.0f, 3.0f, 0.0f);

	constexpr int LANDING = 360;
	constexpr int SETTLED = 60;

	constexpr double MICROSECONDS_PER_MILLISECOND = 1000.0;

	// Xorshift32, which CrowdBenchmark.cs writes out the same way, so both
	// engines drop the same bodies.
	struct Random
	{
		// A float takes the top 24 bits, all its mantissa holds.
		static constexpr int DROPPED_BITS = 8;
		static constexpr float PER_STEP = 1.0f / (1 << 24);

		static constexpr int SHIFT_A = 13;
		static constexpr int SHIFT_B = 17;
		static constexpr int SHIFT_C = 5;

		uint32_t state;

		float Between(float least, float most)
		{
			state ^= state << SHIFT_A;
			state ^= state >> SHIFT_B;
			state ^= state << SHIFT_C;

			return least + (state >> DROPPED_BITS) * PER_STEP * (most - least);
		};
	};

	// Where the results go besides the test output, for the script to read.
	constexpr const char* OUTPUT_VARIABLE = "LOOM_BENCHMARK_OUTPUT";

	Loom::GameObject* Add(Loom::Scene& scene, const std::string& name, const glm::vec3& position)
	{
		Loom::GameObject* object = scene.AddChild(name);
		Pump();

		object->transform.position = position;

		return object;
	};

	void Box(Loom::Scene& scene, const std::string& name, const glm::vec3& position, const glm::vec3& size)
	{
		Add(scene, name, position)->Attach<Loom::BoxCollider>()->size = size;
	};

	std::vector<float> Torus()
	{
		std::vector<float> vertices{ };

		const auto at =
			[](int ring, int tube)
			{
				const float around = glm::two_pi<float>() * ring / RING_SEGMENTS;
				const float turn = glm::two_pi<float>() * tube / TUBE_SEGMENTS;
				const glm::vec3 outward(std::cos(around), std::sin(around), 0.0f);

				return outward * RING_RADIUS
					+ (outward * std::cos(turn) + glm::vec3(0.0f, 0.0f, std::sin(turn))) * TUBE_RADIUS;
			};

		for (int ring = 0; ring < RING_SEGMENTS; ring++)
			for (int tube = 0; tube < TUBE_SEGMENTS; tube++)
				for (const glm::vec3& corner : {
					at(ring, tube), at(ring + 1, tube), at(ring + 1, tube + 1),
					at(ring, tube), at(ring + 1, tube + 1), at(ring, tube + 1) })
					vertices.insert(vertices.end(), { corner.x, corner.y, corner.z });

		return vertices;
	};

	void Ring(Loom::Scene& scene, const std::vector<float>& torus, const glm::vec3& position, const glm::vec3& rotation)
	{
		Loom::GameObject* ring = Add(scene, "Ring", position);
		ring->transform.rotation = rotation;
		ring->transform.scale = glm::vec3(RING_SCALE);
		ring->Attach<Loom::Mesh>()->m_vertices = torus;
		ring->Attach<Loom::MeshCollider>();
	};

	std::vector<Loom::Rigidbody*> Drop(Loom::Scene& scene)
	{
		Random random{ SEED };

		const float jitter = (SPACING - MOST_SIZE) * HALF;

		const int perRow = std::max(1, (int)(SPREAD / HALF / SPACING));
		const int perLayer = perRow * perRow;

		std::vector<Loom::Rigidbody*> bodies{ };

		for (int i = 0; i < COUNT; i++)
		{
			const int cell = i % perLayer;
			const glm::vec3 slot(
				(cell % perRow - (perRow - 1) * HALF) * SPACING,
				ABOVE + (i / perLayer) * SPACING,
				(cell / perRow - (perRow - 1) * HALF) * SPACING);

			// One at a time, in the order Unity's side draws them.
			const float offX = random.Between(-jitter, jitter);
			const float offY = random.Between(-jitter, jitter);
			const float offZ = random.Between(-jitter, jitter);
			const float turnX = random.Between(0.0f, FULL_TURN);
			const float turnY = random.Between(0.0f, FULL_TURN);
			const float turnZ = random.Between(0.0f, FULL_TURN);
			const float size = random.Between(LEAST_SIZE, MOST_SIZE);

			Loom::GameObject* body = Add(scene, "Body", slot + glm::vec3(offX, offY, offZ));
			body->transform.rotation = glm::vec3(turnX, turnY, turnZ);
			body->transform.scale = glm::vec3(size);

			if (i % KINDS == 0)
				body->Attach<Loom::BoxCollider>();
			else
				body->Attach<Loom::SphereCollider>();

			Loom::Rigidbody* rigidbody = body->Attach<Loom::Rigidbody>();
			rigidbody->drag = DRAG;
			bodies.push_back(rigidbody);
		};

		Pump();

		return bodies;
	};

	std::vector<Loom::Ray> Scanner()
	{
		const glm::vec3 forward = glm::normalize(TARGET - EYE);
		const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
		const glm::vec3 up = glm::cross(right, forward);

		const float tall = std::tan(glm::radians(FIELD_OF_VIEW) * HALF);
		const float wide = tall * ASPECT;

		std::vector<Loom::Ray> rays{ };

		for (int row = 0; row < ROWS; row++)
			for (int column = 0; column < COLUMNS; column++)
			{
				const float x = (column + HALF) / (COLUMNS * HALF) - 1.0f;
				const float y = 1.0f - (row + HALF) / (ROWS * HALF);

				rays.push_back({ EYE, glm::normalize(forward + right * (x * wide) + up * (y * tall)) });
			};

		return rays;
	};

	// The variable's value, or empty when it is not set.
	std::string Environment(const char* name)
	{
#ifdef _MSC_VER
		char* value = nullptr;
		size_t length = 0;

		if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
			return "";

		const std::string copy(value);
		free(value);

		return copy;
#else
		const char* value = std::getenv(name);
		return value ? value : "";
#endif
	};

	double MillisecondsSince(const std::chrono::steady_clock::time_point& start)
	{
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	};
};


TEST_SUITE("Benchmark" * doctest::skip())
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the Crowd scene, timed as Unity times it")
	{
		Loom::Scene scene("crowd benchmark");
		Pump();

		const float across = (INSIDE + WALL) / HALF;
		const float wallMiddle = INSIDE + WALL * HALF;
		const float wallUp = WALL_HEIGHT * HALF;

		Box(scene, "Floor", glm::vec3(0.0f, -WALL * HALF, 0.0f), glm::vec3(across, WALL, across));
		Box(scene, "Wall North", glm::vec3(0.0f, wallUp, -wallMiddle), glm::vec3(across, WALL_HEIGHT, WALL));
		Box(scene, "Wall South", glm::vec3(0.0f, wallUp, wallMiddle), glm::vec3(across, WALL_HEIGHT, WALL));
		Box(scene, "Wall East", glm::vec3(wallMiddle, wallUp, 0.0f), glm::vec3(WALL, WALL_HEIGHT, INSIDE / HALF));
		Box(scene, "Wall West", glm::vec3(-wallMiddle, wallUp, 0.0f), glm::vec3(WALL, WALL_HEIGHT, INSIDE / HALF));

		const std::vector<float> torus = Torus();

		for (const Placed& ring : RINGS)
			Ring(scene, torus, ring.position, ring.rotation);

		Pump();

		const std::vector<Loom::Rigidbody*> bodies = Drop(scene);
		const std::vector<Loom::Ray> rays = Scanner();

		double peak = 0.0, total = 0.0, landing = 0.0, settled = 0.0, rayTime = 0.0;
		int rayCount = 0;

		for (int step = 0; step < STEPS; step++)
		{
			auto start = std::chrono::steady_clock::now();
			scene.Physics();
			const double milliseconds = MillisecondsSince(start);

			peak = std::max(peak, milliseconds);
			total += milliseconds;

			if (step < LANDING)
				landing += milliseconds;

			if (step >= STEPS - SETTLED)
				settled += milliseconds;

			const int row = step % ROWS;
			start = std::chrono::steady_clock::now();

			for (int column = 0; column < COLUMNS; column++)
			{
				Loom::RaycastHit hit;
				scene.Raycast(rays[row * COLUMNS + column], hit);
			};

			rayTime += MillisecondsSince(start);
			rayCount += COLUMNS;
		};

		int awake = 0;
		int escaped = 0;

		for (const Loom::Rigidbody* body : bodies)
		{
			const glm::vec3 at(body->GetGameObject()->WorldMatrix()[3]);

			awake += !body->IsSleeping();
			escaped += at.y < 0.0f || std::abs(at.x) > INSIDE || std::abs(at.z) > INSIDE;
		};

		const std::vector<std::string> lines =
		{
			"engine Loom",
			"bodies " + std::to_string(COUNT),
			"steps " + std::to_string(STEPS),
			"peak_step_ms " + std::to_string(peak),
			"mean_step_ms " + std::to_string(total / STEPS),
			"landing_step_ms " + std::to_string(landing / LANDING),
			"settled_step_ms " + std::to_string(settled / SETTLED),
			"ray_us " + std::to_string(rayTime * MICROSECONDS_PER_MILLISECOND / rayCount),
			"awake " + std::to_string(awake),
			"escaped " + std::to_string(escaped),
		};

		std::ostringstream report;

		for (const std::string& line : lines)
			report << "\n  " << line;

		MESSAGE(report.str());

		if (const std::string output = Environment(OUTPUT_VARIABLE); !output.empty())
		{
			std::ofstream file(output);

			for (const std::string& line : lines)
				file << line << '\n';
		};

		CHECK(escaped == 0);
	};
};
