#pragma once

#include "Component.h"
#include "Material.h"
#include "Mesh.h"

#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"

#include <cmath>
#include <string>
#include <vector>


namespace RaycastsScripts
{
	// Draws a unit cube, or a sphere half a unit across, the shapes a
	// BoxCollider and a SphereCollider have before they are resized. Scaling
	// the GameObject sizes the shape and its collider together.
	struct Shape : Loom::Component<Shape>
	{
		void OnAttach() override
		{
			Loom::Material* material = m_gameObject->GetComponent<Loom::Material>();

			if (material == nullptr)
				material = m_gameObject->Attach<Loom::Material>();

			material->SetShaderPath(m_shader);
			material->color = *m_color;

			Loom::Mesh* mesh = m_gameObject->GetComponent<Loom::Mesh>();

			if (mesh == nullptr)
				mesh = m_gameObject->Attach<Loom::Mesh>();

			mesh->material = material;
			mesh->m_vertices = m_sphere ? Sphere() : Cube();
		};

	private:
		static constexpr int CUBE_FACES = 6;
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
				const int axis = face / 2;
				const float side = face % 2 == 0 ? HALF : -HALF;

				glm::vec3 out(0.0f), across(0.0f), up(0.0f);
				out[axis] = side;
				across[(axis + 1) % 3] = side;
				up[(axis + 2) % 3] = HALF;

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
		Loom::Serial<std::string> m_shader = "Assets/Lit.shader";
	};
};
