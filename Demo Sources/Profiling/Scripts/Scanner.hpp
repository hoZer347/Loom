#pragma once

#include "Camera.h"
#include "Component.h"
#include "Material.h"
#include "Mesh.h"
#include "Physics.h"
#include "Scene.h"

#include "glm/glm.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>


namespace ProfilingScripts
{
	// Sweeps a grid of rays out through the scene's camera, a row a frame,
	// and draws a short tick along the surface normal at every hit, so the
	// colliders show as a cloud. Each full sweep is timed and written to the
	// console. Lives on a GameObject that has not been moved: the ticks are
	// in world space.
	struct Scanner : Loom::Component<Scanner>
	{
		void OnAttach() override
		{
			Loom::Material* material = m_gameObject->GetComponent<Loom::Material>();

			if (material == nullptr)
				material = m_gameObject->Attach<Loom::Material>();

			material->SetShaderPath(m_shader);
			material->color = *m_color;

			m_mesh = m_gameObject->GetComponent<Loom::Mesh>();

			if (m_mesh == nullptr)
				m_mesh = m_gameObject->Attach<Loom::Mesh>();

			m_mesh->material = material;
			m_mesh->primitive_id = Loom::Mesh::Lines;
			m_mesh->m_draw_type = Loom::Mesh::Stream;
		};

		void OnUpdate() override
		{
			Loom::Scene* scene = Own();
			Loom::Camera* camera = scene ? scene->GetRoot().FindComponent<Loom::Camera>() : nullptr;

			if (camera == nullptr || m_rows < 1 || m_columns < 1)
				return;

			m_ticks.resize((size_t)m_rows);

			if (m_row >= m_rows)
				m_row = 0;

			// From clip space back into the world, to find each ray's way out.
			const glm::mat4 toWorld = glm::inverse(camera->ViewProjection(ASPECT));

			const auto unproject =
				[&](float x, float y, float depth)
				{
					const glm::vec4 point = toWorld * glm::vec4(x, y, depth, 1.0f);
					return glm::vec3(point) / point.w;
				};

			const float y = 1.0f - NDC_SPAN * (m_row + HALF) / m_rows;
			std::vector<float>& ticks = m_ticks[m_row];
			ticks.clear();

			const auto start = std::chrono::steady_clock::now();

			for (int column = 0; column < m_columns; column++)
			{
				const float x = NDC_SPAN * (column + HALF) / m_columns - 1.0f;
				const glm::vec3 near = unproject(x, y, -1.0f);
				const glm::vec3 far = unproject(x, y, 1.0f);

				Loom::RaycastHit hit;

				if (!scene->Raycast({ near, far - near }, hit))
					continue;

				const glm::vec3 tip = hit.point + hit.normal * *m_tickLength;
				ticks.insert(ticks.end(), { hit.point.x, hit.point.y, hit.point.z, tip.x, tip.y, tip.z });
			};

			m_sweepTime += std::chrono::steady_clock::now() - start;

			std::vector<float> lines{ };

			for (const std::vector<float>& row : m_ticks)
				lines.insert(lines.end(), row.begin(), row.end());

			m_mesh->m_vertices = lines;

			if (++m_row < m_rows)
				return;

			const double milliseconds = std::chrono::duration<double, std::milli>(m_sweepTime).count();
			const int rays = m_rows * m_columns;

			std::cout << "Swept " << rays << " rays among " << scene->GetColliders().Size()
				<< " colliders in " << milliseconds << " ms, "
				<< milliseconds * MICROSECONDS_PER_MILLISECOND / rays << " us a ray" << std::endl;

			m_row = 0;
			m_sweepTime = { };
		};

	private:
		static constexpr float ASPECT = 16.0f / 9.0f;
		static constexpr float HALF = 0.5f;

		// Clip space runs from -1 to 1 across the view.
		static constexpr float NDC_SPAN = 2.0f;
		static constexpr double MICROSECONDS_PER_MILLISECOND = 1000.0;

		// The scene this is in, found by its root.
		Loom::Scene* Own() const
		{
			const Loom::GameObject* root = m_gameObject;

			while (root->GetParent())
				root = root->GetParent();

			for (Loom::Scene* scene : Loom::Scene::GetScenes())
				if (&scene->GetRoot() == root)
					return scene;

			return nullptr;
		};

		Loom::Serial<int> m_columns = 160;
		Loom::Serial<int> m_rows = 90;
		Loom::Serial<float> m_tickLength = 0.25f;
		Loom::Serial<glm::vec3> m_color = glm::vec3(1.0f, 0.95f, 0.3f);
		Loom::Serial<std::string> m_shader = "Assets/Beam.shader";

		Loom::Mesh* m_mesh = nullptr;
		std::vector<std::vector<float>> m_ticks{ };
		int m_row = 0;
		std::chrono::steady_clock::duration m_sweepTime{ };
	};
};
