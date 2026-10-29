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


namespace PhysicsScripts
{
	// Draws a unit cube, or a sphere half a unit across, the shapes a
	// BoxCollider and a SphereCollider have before they are resized, or a
	// ring half a unit across around Z for a MeshCollider to follow. Scaling
	// the GameObject sizes the shape and its collider together.
	struct Shape : Loom::Component<Shape>
	{
		enum Kind { Cube, Sphere, Torus };

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
			mesh->m_vertices = Build(m_kind);
			mesh->m_normals = Normals(m_kind, *mesh->m_vertices);
		};

		// For a Shape made by a script, before it is first drawn.
		void Configure(Kind kind, const glm::vec3& colour)
		{
			m_kind = kind;
			m_color = colour;
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
		#pragma region Building

		static std::vector<float> Build(Kind kind)
		{
			switch (kind)
			{
			case Sphere:	return SphereVertices();
			case Torus:		return TorusVertices();
			default:		return CubeVertices();
			};
		};

		static void Add(std::vector<float>& vertices, const glm::vec3& point)
		{
			vertices.insert(vertices.end(), { point.x, point.y, point.z });
		};

		// Smooth over the round shapes, each vertex's normal pointing out from
		// the middle of the sphere or of the ring's tube. None for the cube,
		// which the engine shades flat.
		static std::vector<float> Normals(Kind kind, const std::vector<float>& vertices)
		{
			std::vector<float> normals{ };

			if (kind == Cube)
				return normals;

			for (size_t i = 0; i < vertices.size(); i += glm::vec3::length())
			{
				const glm::vec3 point(vertices[i], vertices[i + 1], vertices[i + 2]);
				const glm::vec3 middle = kind == Torus
					? glm::normalize(glm::vec3(point.x, point.y, 0.0f)) * RING_RADIUS
					: glm::vec3(0.0f);

				Add(normals, glm::normalize(point - middle));
			};

			return normals;
		};

		static void AddQuad(std::vector<float>& vertices, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d)
		{
			for (const glm::vec3& point : { a, b, c, a, c, d })
				Add(vertices, point);
		};

		#pragma endregion

		#pragma region Shapes

		static constexpr int CUBE_FACES = 6;
		static constexpr int FACES_PER_AXIS = 2;
		static constexpr int SPHERE_STACKS = 16;
		static constexpr int SPHERE_SLICES = 32;
		static constexpr float HALF = 0.5f;
		static constexpr int RING_SEGMENTS = 48;
		static constexpr int TUBE_SEGMENTS = 16;
		static constexpr float RING_RADIUS = 0.35f;
		static constexpr float TUBE_RADIUS = 0.15f;

		static std::vector<float> CubeVertices()
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

		static std::vector<float> SphereVertices()
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

		static std::vector<float> TorusVertices()
		{
			std::vector<float> vertices{ };

			const auto at =
				[](int ring, int tube)
				{
					const float around = glm::two_pi<float>() * ring / RING_SEGMENTS;
					const float turn = glm::two_pi<float>() * tube / TUBE_SEGMENTS;

					const glm::vec3 outward(std::cos(around), std::sin(around), 0.0f);

					return outward * RING_RADIUS
						+ (outward * std::cos(turn) + glm::vec3(0.0f, 0.0f, std::sin(turn))) * TUBE_RADIUS;
				};

			for (int ring = 0; ring < RING_SEGMENTS; ring++)
				for (int tube = 0; tube < TUBE_SEGMENTS; tube++)
					AddQuad(vertices,
						at(ring, tube),
						at(ring + 1, tube),
						at(ring + 1, tube + 1),
						at(ring, tube + 1));

			return vertices;
		};

		#pragma endregion

		Loom::Serial<Kind> m_kind = Cube;
		Loom::Serial<glm::vec3> m_color = glm::vec3(0.8f, 0.8f, 0.8f);
		Loom::Serial<Loom::Asset<".shader">> m_shader = "Assets/Lit.shader";

		Loom::Material* m_material = nullptr;
		std::optional<glm::vec3> m_tint{ };
	};
};
