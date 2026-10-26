#pragma once

#include "Component.h"
#include "Material.h"
#include "Mesh.h"
#include "Utilities/Clock.h"

#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"

#include <array>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>


namespace UVLayersScripts
{
	// Hills on a grid, coloured by three UV layers its shader reads:
	// UV_1<vec3 tint, float glow>, set again every frame as a ring of glow
	// spreads from the middle, UV_2<int band> for each triangle's height band
	// and UV_3<bool> on the grid lines.
	struct Terrain : Loom::Component<Terrain>
	{
		void OnAttach() override
		{
			Loom::Material* material = m_gameObject->GetComponent<Loom::Material>();

			if (material == nullptr)
				material = m_gameObject->Attach<Loom::Material>();

			material->SetShaderPath(m_shader);

			m_mesh = m_gameObject->GetComponent<Loom::Mesh>();

			if (m_mesh == nullptr)
				m_mesh = m_gameObject->Attach<Loom::Mesh>();

			m_mesh->material = material;
			m_mesh->m_generated = true;
			m_mesh->m_draw_type = Loom::Mesh::Dynamic;

			Build();
			Glow();
		};

		void OnUpdate() override
		{
			m_elapsed += Loom::Time::DeltaTime();
			Glow();
		};

	private:
		static constexpr int CORNERS_PER_TRIANGLE = 3;
		static constexpr int CORNERS_PER_CELL = 6;
		static constexpr float HALF = 0.5f;
		static constexpr float SECOND_HILL_SCALE = 0.45f;
		static constexpr float SECOND_HILL_FREQUENCY = 2.3f;
		static constexpr float Z_FREQUENCY = 0.8f;
		static constexpr glm::vec3 LOW = glm::vec3(0.25f, 0.55f, 0.3f);
		static constexpr glm::vec3 HIGH = glm::vec3(0.85f, 0.8f, 0.7f);

		float Height(float x, float z) const
		{
			return *m_hill * (std::sin(x * *m_frequency) * std::cos(z * *m_frequency * Z_FREQUENCY)
				+ SECOND_HILL_SCALE * std::sin((x + z) * *m_frequency * SECOND_HILL_FREQUENCY));
		};

		void Build()
		{
			std::vector<float> vertices{ };
			std::vector<float> normals{ };
			std::vector<int> bands{ };
			std::vector<bool> lines{ };

			m_tints.clear();
			m_corners.clear();

			const int cells = *m_cells;
			const float size = *m_cellSize;
			const float extent = *m_hill * (1.0f + SECOND_HILL_SCALE);

			const size_t corners = (size_t)cells * cells * CORNERS_PER_CELL;
			vertices.reserve(corners * glm::vec3::length());
			normals.reserve(corners * glm::vec3::length());

			const auto at = [&](int i, int j)
			{
				const float x = (i - cells * HALF) * size;
				const float z = (j - cells * HALF) * size;

				return glm::vec3(x, Height(x, z), z);
			};

			for (int i = 0; i < cells; i++)
				for (int j = 0; j < cells; j++)
				{
					const glm::vec3 a = at(i, j);
					const glm::vec3 b = at(i, j + 1);
					const glm::vec3 c = at(i + 1, j + 1);
					const glm::vec3 d = at(i + 1, j);
					const bool line = i % *m_lineEvery == 0 || j % *m_lineEvery == 0;

					const std::array<std::array<glm::vec3, CORNERS_PER_TRIANGLE>, 2> triangles = { { { a, b, c }, { a, c, d } } };

					for (const std::array<glm::vec3, CORNERS_PER_TRIANGLE>& triangle : triangles)
					{
						const glm::vec3 normal = glm::normalize(glm::cross(triangle[1] - triangle[0], triangle[2] - triangle[0]));
						const float middle = (triangle[0].y + triangle[1].y + triangle[2].y) / CORNERS_PER_TRIANGLE;
						const int band = (int)std::floor((middle + extent) / *m_bandHeight);

						for (const glm::vec3& point : triangle)
						{
							vertices.insert(vertices.end(), { point.x, point.y, point.z });
							normals.insert(normals.end(), { normal.x, normal.y, normal.z });
							bands.push_back(band);
							lines.push_back(line);

							m_tints.push_back(glm::mix(LOW, HIGH, glm::clamp(HALF * (point.y / extent + 1.0f), 0.0f, 1.0f)));
							m_corners.push_back(glm::vec2(point.x, point.z));
						};
					};
				};

			m_mesh->m_vertices = std::move(vertices);
			m_mesh->m_normals = std::move(normals);
			m_mesh->SetUV(2, bands);
			m_mesh->SetUV(3, lines);
		};

		void Glow()
		{
			const float radius = std::fmod(m_elapsed * *m_glowSpeed, *m_cells * *m_cellSize * HALF);

			std::vector<std::tuple<glm::vec3, float>> tinted{ };
			tinted.reserve(m_corners.size());

			for (size_t i = 0; i < m_corners.size(); i++)
			{
				const float off = std::abs(glm::length(m_corners[i]) - radius);
				tinted.emplace_back(m_tints[i], glm::max(0.0f, 1.0f - off / *m_glowWidth));
			};

			m_mesh->SetUV(1, tinted);
		};

		Loom::Serial<int> m_cells = 48;
		// Units across a cell.
		Loom::Serial<float> m_cellSize = 0.4f;
		// Units the main hills rise above, and fall below, the middle height.
		Loom::Serial<float> m_hill = 1.2f;
		Loom::Serial<float> m_frequency = 0.35f;
		// Units of height each band spans.
		Loom::Serial<float> m_bandHeight = 0.4f;
		Loom::Serial<int> m_lineEvery = 8;
		// Units a second the ring of glow spreads.
		Loom::Serial<float> m_glowSpeed = 3.0f;
		// Units from the ring's middle out to where its glow ends.
		Loom::Serial<float> m_glowWidth = 1.0f;
		Loom::Serial<Loom::Asset<".shader">> m_shader = "Assets/Terrain.shader";

		Loom::Mesh* m_mesh = nullptr;
		std::vector<glm::vec3> m_tints;
		std::vector<glm::vec2> m_corners;
		float m_elapsed = 0.0f;
	};
};
