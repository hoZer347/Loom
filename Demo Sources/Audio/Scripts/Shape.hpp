#pragma once

#include "Component.h"
#include "Material.h"
#include "Mesh.h"

#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <vector>


namespace AudioScripts
{
	// Draws a unit cube, or a sphere half a unit across, sized by scaling
	// the GameObject.
	struct Shape : Loom::Component<Shape>
	{
		void OnAttach() override
		{
			m_material = m_gameObject->GetComponent<Loom::Material>();

			if (m_material == nullptr)
				m_material = m_gameObject->Attach<Loom::Material>();

			m_material->SetShaderPath(m_shader);
			m_material->color = *m_color;

			Loom::Mesh* mesh = m_gameObject->GetComponent<Loom::Mesh>();

			if (mesh == nullptr)
				mesh = m_gameObject->Attach<Loom::Mesh>();

			mesh->material = m_material;
			mesh->m_vertices = m_sphere ? Sphere() : Cube();
		};

		// Drawn in colour from its next update, and in its own again at the
		// update after unless asked again.
		void Tint(const glm::vec3& colour)
		{
			m_tint = colour;
		};

		void OnUpdate() override
		{
			m_material->color = m_tint.value_or(*m_color);
			m_tint.reset();
		};

	private:
		static constexpr int CUBE_FACES = 6;
		static constexpr int FACES_PER_AXIS = 2;
		static constexpr int SPHERE_STACKS = 16;
		static constexpr int SPHERE_SLICES = 32;
		static constexpr float HALF = 0.5f;

		static void Add(std::vector<float>& vertices, const glm::vec3& point)
		{
			vertices.insert(vertices.end(), { point.x, point.y, point.z });
		};

		static void AddQuad(std::vector<float>& vertices, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d)
		{
			for (const glm::vec3& point : { a, b, c, a, c, d })
				Add(vertices, point);
		};

		static std::vector<float> Cube()
		{
			std::vector<float> vertices{ };

			// Each face looks out along one axis, either way; across and up
			// are the other two, turned so the corners go anticlockwise seen
			// from outside.
			for (int face = 0; face < CUBE_FACES; face++)
			{
				const int axis = face / FACES_PER_AXIS;
				const float side = face % FACES_PER_AXIS == 0 ? HALF : -HALF;

				glm::vec3 out(0.0f), across(0.0f), up(0.0f);
				out[axis] = side;
				across[(axis + 1) % glm::vec3::length()] = side;
				up[(axis + 2) % glm::vec3::length()] = HALF;

				AddQuad(vertices, out - across - up, out + across - up, out + across + up, out - across + up);
			};

			return vertices;
		};

		static std::vector<float> Sphere()
		{
			std::vector<float> vertices{ };

			const auto at =
				[](int stack, int slice)
				{
					const float down = glm::pi<float>() * stack / SPHERE_STACKS;
					const float around = glm::two_pi<float>() * slice / SPHERE_SLICES;

					return HALF * glm::vec3(
						std::sin(down) * std::cos(around),
						std::cos(down),
						-std::sin(down) * std::sin(around));
				};

			for (int stack = 0; stack < SPHERE_STACKS; stack++)
				for (int slice = 0; slice < SPHERE_SLICES; slice++)
					AddQuad(vertices,
						at(stack, slice),
						at(stack + 1, slice),
						at(stack + 1, slice + 1),
						at(stack, slice + 1));

			return vertices;
		};

		Loom::Serial<bool> m_sphere;
		Loom::Serial<glm::vec3> m_color = glm::vec3(0.8f, 0.8f, 0.8f);
		Loom::Serial<Loom::Asset<".shader">> m_shader = "Assets/Lit.shader";

		Loom::Material* m_material = nullptr;
		std::optional<glm::vec3> m_tint{ };
	};
};
