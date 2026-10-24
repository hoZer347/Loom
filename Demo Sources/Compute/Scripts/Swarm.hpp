#pragma once

#include "Component.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Scene.h"
#include "Shaders.h"
#include "Utilities/Clock.h"

#include "glm/glm.hpp"

#include <cmath>
#include <memory>
#include <random>
#include <string>
#include <vector>


namespace ComputeScripts
{
	// Particles that Swarm.shader moves on the GPU each frame and
	// Particle.shader draws straight from the buffer it moved them in, the
	// same on every renderer with compute. They keep still until the game
	// plays.
	struct Swarm : Loom::Component<Swarm>
	{
		~Swarm()
		{
			if (Loom::Renderer* renderer = Loom::Renderer::Get(); renderer && m_particles)
				renderer->DestroyBuffer(m_particles);
		};

		void OnUpdate() override
		{
			m_unstepped += Loom::Time::DeltaTime();
		};

		void OnRender() override
		{
			Loom::Renderer* renderer = Loom::Renderer::Get();

			if (!renderer || !renderer->SupportsCompute() || !Ready(*renderer))
				return;

			const uint32_t step = m_step->id;
			const uint32_t draw = m_draw->id;

			renderer->SetBuffer(step, PARTICLES, m_particles);
			renderer->SetBuffer(draw, PARTICLES, m_particles);

			// Once a frame, however many views draw it.
			if (m_unstepped > 0.0f)
			{
				m_time += m_unstepped;
				renderer->SetUniform(step, "u_time", m_time);
				renderer->SetUniform(step, "u_deltaTime", m_unstepped);
				renderer->Dispatch(step, (uint32_t)m_seeded, 1, 1);
				m_unstepped = 0.0f;
			};

			Loom::Scene::ApplyView(draw);
			renderer->SetUniform(draw, "u_model", m_gameObject->WorldMatrix());
			renderer->Draw(draw, Loom::Mesh::Triangles, nullptr, (size_t)m_seeded * GROUP_SIZE * CORNERS, Loom::Mesh::Stream);
		};

	private:
		static constexpr const char* PARTICLES = "Particles";

		// Swarm.shader's local_size_x.
		static constexpr uint32_t GROUP_SIZE = 64;

		// Particle.shader's two triangles to a particle.
		static constexpr size_t CORNERS = 6;

		static constexpr float RADIUS = 4.0f;
		static constexpr unsigned SEED = 1;

		// Compiles the shaders the first time, and seeds the particles then
		// and again whenever the number of groups changes.
		bool Ready(Loom::Renderer& renderer)
		{
			if (!m_step)
			{
				m_step = std::make_unique<Loom::Shader>(*m_stepPath);
				m_draw = std::make_unique<Loom::Shader>(*m_drawPath);
			};

			if (!m_step->id || !m_step->compute || !m_draw->id || *m_groups < 1)
				return false;

			if (m_particles && m_seeded == *m_groups)
				return true;

			if (m_particles)
				renderer.DestroyBuffer(m_particles);

			// A ball of them, each already circling the GameObject's up axis.
			std::mt19937 random(SEED);
			std::normal_distribution<float> normal;
			std::uniform_real_distribution<float> uniform;
			std::vector<glm::vec4> particles;

			for (uint32_t i = 0; i < (uint32_t)*m_groups * GROUP_SIZE; i++)
			{
				const glm::vec3 position = glm::normalize(glm::vec3(normal(random), normal(random), normal(random))) * RADIUS * std::cbrt(uniform(random));

				particles.emplace_back(position, 1.0f);
				particles.emplace_back(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), position), 0.0f);
			};

			const size_t size = particles.size() * sizeof(glm::vec4);
			m_particles = renderer.CreateBuffer(size);
			renderer.SetBufferData(m_particles, particles.data(), size);
			m_seeded = *m_groups;

			return m_particles != 0;
		};

		Loom::Serial<std::string> m_stepPath = std::string("Assets/Swarm.shader");
		Loom::Serial<std::string> m_drawPath = std::string("Assets/Particle.shader");

		Loom::Serial<int> m_groups = 256;

		std::unique_ptr<Loom::Shader> m_step;
		std::unique_ptr<Loom::Shader> m_draw;
		uint32_t m_particles = 0;
		int m_seeded = 0;
		float m_time = 0.0f;
		float m_unstepped = 0.0f;
	};
};
