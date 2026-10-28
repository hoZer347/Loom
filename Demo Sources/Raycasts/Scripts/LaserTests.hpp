#pragma once

#include "Laser.hpp"

#include "Collider.h"
#include "Engine.h"
#include "Mesh.h"
#include "Physics.h"
#include "Scene.h"
#include "ScriptTests.h"

#include "glm/glm.hpp"


namespace RaycastsScripts
{
	// Cases for the Tests panel. Each builds a scene of its own, far above
	// the one open in the editor so its rays meet only what the case placed.
	namespace LaserTests
	{
		constexpr glm::vec3 ORIGIN{ 0.0f, 1000.0f, 0.0f };
		constexpr glm::vec3 AHEAD{ 0.0f, 0.0f, -1.0f };
		constexpr float TOLERANCE = 0.001f;

		inline Loom::BoxCollider* PlaceBox(Loom::Scene& scene, const glm::vec3& position)
		{
			Loom::GameObject* box = scene.AddChild("Box");
			Loom::Engine::DoTasks();

			box->transform.position = position;

			Loom::BoxCollider* collider = box->Attach<Loom::BoxCollider>();
			Loom::Engine::DoTasks();

			return collider;
		};

		LOOM_TEST("A ray stops at the first box in its way")
		{
			constexpr float NEAR_DISTANCE = 5.0f;
			constexpr float FAR_DISTANCE = 9.0f;
			constexpr float HALF_BOX = 0.5f;

			Loom::Scene scene("Laser Tests");
			Loom::Engine::DoTasks();

			Loom::BoxCollider* nearer = PlaceBox(scene, ORIGIN + AHEAD * NEAR_DISTANCE);
			PlaceBox(scene, ORIGIN + AHEAD * FAR_DISTANCE);
			scene.Physics();

			Loom::RaycastHit hit;

			LOOM_CHECK(Loom::Physics::Raycast(ORIGIN, AHEAD, hit));
			LOOM_CHECK(hit.collider == nearer);
			LOOM_CHECK(glm::abs(hit.distance - (NEAR_DISTANCE - HALF_BOX)) < TOLERANCE);
		};

		LOOM_TEST("A ray misses a box behind it")
		{
			constexpr float BEHIND = 5.0f;

			Loom::Scene scene("Laser Tests");
			Loom::Engine::DoTasks();

			PlaceBox(scene, ORIGIN - AHEAD * BEHIND);
			scene.Physics();

			Loom::RaycastHit hit;

			LOOM_CHECK(!Loom::Physics::Raycast(ORIGIN, AHEAD, hit));
		};

		LOOM_TEST("A laser with nothing ahead draws its beam to its full range")
		{
			constexpr float RANGE = 12.0f;
			constexpr size_t BEAM_FLOATS = 6;

			Loom::Scene scene("Laser Tests");
			Loom::Engine::DoTasks();

			Loom::GameObject* object = scene.AddChild("Laser");
			Loom::Engine::DoTasks();

			object->transform.position = ORIGIN;
			object->Attach<Laser>();
			Loom::Engine::DoTasks();

			const Loom::Mesh* mesh = object->GetComponent<Loom::Mesh>();

			LOOM_CHECK(mesh != nullptr);

			if (mesh)
			{
				const std::vector<float>& beam = mesh->m_vertices;

				LOOM_CHECK(beam.size() == BEAM_FLOATS);
				LOOM_CHECK(glm::abs(beam.back() + RANGE) < TOLERANCE);
			};
		};
	};
};
