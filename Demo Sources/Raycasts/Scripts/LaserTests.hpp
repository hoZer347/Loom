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
			constexpr float near_distance = 5.0f;
			constexpr float far_distance = 9.0f;
			constexpr float half_box = 0.5f;

			Loom::Scene scene("Laser Tests");
			Loom::Engine::DoTasks();

			Loom::BoxCollider* nearer = PlaceBox(scene, ORIGIN + AHEAD * near_distance);
			PlaceBox(scene, ORIGIN + AHEAD * far_distance);
			scene.Physics();

			Loom::RaycastHit hit;

			LOOM_CHECK(Loom::Physics::Raycast(ORIGIN, AHEAD, hit));
			LOOM_CHECK(hit.collider == nearer);
			LOOM_CHECK(glm::abs(hit.distance - (near_distance - half_box)) < TOLERANCE);
		};

		LOOM_TEST("A ray misses a box behind it")
		{
			constexpr float behind = 5.0f;

			Loom::Scene scene("Laser Tests");
			Loom::Engine::DoTasks();

			PlaceBox(scene, ORIGIN - AHEAD * behind);
			scene.Physics();

			Loom::RaycastHit hit;

			LOOM_CHECK(!Loom::Physics::Raycast(ORIGIN, AHEAD, hit));
		};

		LOOM_TEST("A laser with nothing ahead draws its beam to its full range")
		{
			constexpr float range = 12.0f;
			constexpr size_t beam_floats = 6;

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

				LOOM_CHECK(beam.size() == beam_floats);
				LOOM_CHECK(glm::abs(beam.back() + range) < TOLERANCE);
			};
		};
	};
};
