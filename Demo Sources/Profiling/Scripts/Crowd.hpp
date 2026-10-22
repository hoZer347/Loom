#pragma once

#include "Collider.h"
#include "Component.h"
#include "Rigidbody.h"
#include "Shape.hpp"

#include "glm/glm.hpp"

#include <algorithm>
#include <random>
#include <string>


namespace ProfilingScripts
{
	// Once the scene starts running, drops count boxes and balls, each with
	// its collider and a Rigidbody, from a column spread either side of it
	// and starting above it, stacked a layer at a time so no two start
	// inside each other, to fall through air and pile up on whatever is below.
	struct Crowd : Loom::Component<Crowd>
	{
		void OnUpdate() override
		{
			if (!m_spawned)
				Spawn();
		};

	private:
		// Boxes and balls in turn.
		static constexpr int KINDS = 2;
		static constexpr unsigned SEED = 1;

		static constexpr float HALF = 0.5f;
		static constexpr float LEAST_SIZE = 0.4f;
		static constexpr float MOST_SIZE = 1.4f;

		// Room each starts in: the largest, turned any way, still fits.
		static constexpr float SPACING = MOST_SIZE * 1.8f;
		static constexpr float FULL_TURN = 360.0f;

		static constexpr glm::vec3 COLOURS[] =
		{
			{ 0.85f, 0.35f, 0.25f },
			{ 0.3f, 0.5f, 0.85f },
			{ 0.35f, 0.75f, 0.45f },
			{ 0.75f, 0.6f, 0.85f },
			{ 0.3f, 0.7f, 0.75f },
		};

		void Spawn()
		{
			m_spawned = true;

			std::mt19937 random(SEED);
			const int perRow = std::max(1, (int)(2.0f * *m_spread / SPACING));
			const int perLayer = perRow * perRow;

			// A little off the grid, so they do not fall in lockstep.
			std::uniform_real_distribution<float> jitter(-(SPACING - MOST_SIZE) * HALF, (SPACING - MOST_SIZE) * HALF);
			std::uniform_real_distribution<float> size(LEAST_SIZE, MOST_SIZE);
			std::uniform_real_distribution<float> angle(0.0f, FULL_TURN);
			std::uniform_int_distribution<size_t> colour(0, std::size(COLOURS) - 1);

			for (int i = 0; i < m_count; i++)
			{
				Loom::GameObject* object = m_gameObject->AddChild("Crowd " + std::to_string(i));

				const int cell = i % perLayer;
				const glm::vec3 slot(
					(cell % perRow - (perRow - 1) * HALF) * SPACING,
					*m_above + (i / perLayer) * SPACING,
					(cell / perRow - (perRow - 1) * HALF) * SPACING);

				object->transform.position = slot + glm::vec3(jitter(random), jitter(random), jitter(random));
				object->transform.rotation = glm::vec3(angle(random), angle(random), angle(random));
				object->transform.scale = glm::vec3(size(random));

				Shape* shape = object->Attach<Shape>();

				if (i % KINDS == 0)
				{
					shape->Configure(Shape::Cube, COLOURS[colour(random)]);
					object->Attach<Loom::BoxCollider>();
				}
				else
				{
					shape->Configure(Shape::Sphere, COLOURS[colour(random)]);
					object->Attach<Loom::SphereCollider>();
				};

				object->Attach<Loom::Rigidbody>()->drag = *m_drag;
			};
		};

		Loom::Serial<int> m_count = 1500;
		Loom::Serial<float> m_spread = 10.0f;
		// Clear of anything in the column, so nothing starts inside it.
		Loom::Serial<float> m_above = 12.0f;
		// Air, which holds a fall to gravity over drag.
		Loom::Serial<float> m_drag = 0.5f;

		bool m_spawned = false;
	};
};
