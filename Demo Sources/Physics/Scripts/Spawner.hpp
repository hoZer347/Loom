#pragma once

#include "Collider.h"
#include "Component.h"
#include "Rigidbody.h"
#include "Shape.hpp"
#include "Utilities/Clock.h"

#include "glm/glm.hpp"

#include <deque>
#include <random>
#include <string>


namespace PhysicsScripts
{
	// Drops a box or a ball, each with its collider and a Rigidbody, from
	// somewhere within spread of from every interval, and lets the oldest go
	// once more than most are down, so the pile keeps moving without growing.
	// One that falls off the world goes too.
	struct Spawner : Loom::Component<Spawner>
	{
		void OnUpdate() override
		{
			m_wait -= Loom::Time::DeltaTime();

			if (m_wait <= 0.0f)
			{
				m_wait += *m_interval;
				Drop(*m_from + Scatter());
			};

			std::erase_if(m_bodies,
				[this](Loom::GameObject* body)
				{
					if (body->transform.position->y > LOST)
						return false;

					m_gameObject->RemoveChild(body);
					return true;
				});

			while ((int)m_bodies.size() > *m_most)
			{
				m_gameObject->RemoveChild(m_bodies.front());
				m_bodies.pop_front();
			};
		};

	private:
		// Boxes and balls in turn.
		static constexpr int KINDS = 2;
		static constexpr unsigned SEED = 1;

		static constexpr float LEAST_SIZE = 0.35f;
		static constexpr float MOST_SIZE = 0.9f;
		static constexpr float FULL_TURN = 360.0f;

		// Below the floor, out of sight.
		static constexpr float LOST = -10.0f;

		static constexpr glm::vec3 COLOURS[] =
		{
			{ 0.85f, 0.35f, 0.25f },
			{ 0.3f, 0.5f, 0.85f },
			{ 0.35f, 0.75f, 0.45f },
			{ 0.75f, 0.6f, 0.85f },
			{ 0.3f, 0.7f, 0.75f },
			{ 0.9f, 0.7f, 0.3f },
		};

		glm::vec3 Scatter()
		{
			std::uniform_real_distribution<float> offset(-*m_spread, *m_spread);

			return { offset(m_random), 0.0f, offset(m_random) };
		};

		void Drop(const glm::vec3& at)
		{
			std::uniform_real_distribution<float> size(LEAST_SIZE, MOST_SIZE);
			std::uniform_real_distribution<float> angle(0.0f, FULL_TURN);
			std::uniform_int_distribution<size_t> colour(0, std::size(COLOURS) - 1);

			Loom::GameObject* body = m_gameObject->AddChild("Body " + std::to_string(m_dropped));

			body->transform.position = at;
			body->transform.rotation = glm::vec3(angle(m_random), angle(m_random), angle(m_random));
			body->transform.scale = glm::vec3(size(m_random));

			Shape* shape = body->Attach<Shape>();

			if (m_dropped % KINDS == 0)
			{
				shape->Configure(Shape::Cube, COLOURS[colour(m_random)]);
				body->Attach<Loom::BoxCollider>();
			}
			else
			{
				shape->Configure(Shape::Sphere, COLOURS[colour(m_random)]);
				body->Attach<Loom::SphereCollider>();
			};

			body->Attach<Loom::Rigidbody>()->bounciness = *m_bounciness;

			m_bodies.push_back(body);
			m_dropped++;
		};

		Loom::Serial<glm::vec3> m_from = glm::vec3(0.0f, 9.0f, 0.0f);
		Loom::Serial<float> m_spread = 1.5f;
		// Seconds between drops.
		Loom::Serial<float> m_interval = 0.25f;
		Loom::Serial<int> m_most = 120;
		Loom::Serial<float> m_bounciness = 0.3f;

		std::deque<Loom::GameObject*> m_bodies{ };
		std::mt19937 m_random{ SEED };
		float m_wait = 0.0f;
		int m_dropped = 0;
	};
};
