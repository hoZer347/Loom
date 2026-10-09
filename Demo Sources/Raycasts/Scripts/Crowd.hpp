#pragma once

#include "Collider.h"
#include "Component.h"
#include "MeshCollider.h"
#include "Shape.hpp"

#include "glm/glm.hpp"

#include <random>
#include <string>


namespace RaycastsScripts
{
	// Fills a cube of spread either side of it with count shapes, each with
	// the collider that fits it, once the scene starts running, then turns
	// the lot about Y so every collider moves every frame.
	struct Crowd : Loom::Component<Crowd>
	{
		void OnUpdate() override
		{
			if (!m_spawned)
				Spawn();

			glm::vec3& rotation = *m_gameObject->transform.rotation;
			rotation.y += m_speed;
		};

	private:
		// One shape in this many is a ring, the rest boxes and balls in turn.
		static constexpr int RING_EVERY = 10;
		static constexpr int KINDS = 2;
		static constexpr unsigned SEED = 1;

		static constexpr float LEAST_SIZE = 0.4f;
		static constexpr float MOST_SIZE = 1.4f;
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
			std::uniform_real_distribution<float> place(-*m_spread, *m_spread);
			std::uniform_real_distribution<float> size(LEAST_SIZE, MOST_SIZE);
			std::uniform_real_distribution<float> angle(0.0f, FULL_TURN);
			std::uniform_int_distribution<size_t> colour(0, std::size(COLOURS) - 1);

			for (int i = 0; i < m_count; i++)
			{
				Loom::GameObject* object = m_gameObject->AddChild("Crowd " + std::to_string(i));

				object->transform.position = glm::vec3(place(random), place(random), place(random));
				object->transform.rotation = glm::vec3(angle(random), angle(random), angle(random));
				object->transform.scale = glm::vec3(size(random));

				Shape* shape = object->Attach<Shape>();

				if (i % RING_EVERY == 0)
				{
					shape->Configure(Shape::Torus, COLOURS[colour(random)]);
					object->Attach<Loom::MeshCollider>();
				}
				else if (i % KINDS == 0)
				{
					shape->Configure(Shape::Cube, COLOURS[colour(random)]);
					object->Attach<Loom::BoxCollider>();
				}
				else
				{
					shape->Configure(Shape::Sphere, COLOURS[colour(random)]);
					object->Attach<Loom::SphereCollider>();
				};
			};
		};

		Loom::Serial<int> m_count = 1500;
		Loom::Serial<float> m_spread = 12.0f;
		// Degrees a frame.
		Loom::Serial<float> m_speed = 0.1f;

		bool m_spawned = false;
	};
};
