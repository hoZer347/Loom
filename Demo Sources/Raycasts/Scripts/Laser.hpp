#pragma once

#include "Collider.h"
#include "Component.h"
#include "Material.h"
#include "Mesh.h"
#include "Physics.h"

#include "glm/glm.hpp"

#include <string>
#include <vector>


namespace RaycastsScripts
{
	// Turns about its Y axis, casting a ray ahead of it every frame with
	// Loom::Physics::Raycast. It draws the ray as far as the first collider it
	// meets, with that surface's normal sticking out of the hit, and tints
	// whatever it is hitting.
	struct Laser : Loom::Component<Laser>
	{
		void OnAttach() override
		{
			Loom::Material* material = m_gameObject->GetComponent<Loom::Material>();

			if (material == nullptr)
				material = m_gameObject->Attach<Loom::Material>();

			material->SetShaderPath(m_shader);
			material->color = *m_beamColor;

			m_mesh = m_gameObject->GetComponent<Loom::Mesh>();

			if (m_mesh == nullptr)
				m_mesh = m_gameObject->Attach<Loom::Mesh>();

			m_mesh->material = material;
			m_mesh->primitive_id = Loom::Mesh::Lines;

			Aim(m_range);
		};

		void OnDetach() override
		{
			Untint();
		};

		void OnUpdate() override
		{
			glm::vec3& rotation = *m_gameObject->transform.rotation;
			rotation.y += m_speed;

			const glm::mat4 world = m_gameObject->WorldMatrix();
			const glm::vec3 origin(world[3]);
			const glm::vec3 forward = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

			Loom::RaycastHit hit;

			if (!Loom::Physics::Raycast(origin, forward, hit, m_range))
			{
				Untint();
				Aim(m_range);
				return;
			};

			Loom::Material* struck = hit.gameObject->GetComponent<Loom::Material>();

			if (struck != m_tinted)
			{
				Untint();

				if (struck)
				{
					m_untinted = *struck->color;
					struck->color = *m_hitColor;
				};

				m_tinted = struck;
			};

			const glm::mat4 toLocal = glm::inverse(world);
			const glm::vec3 normalEnd(toLocal * glm::vec4(hit.point + hit.normal * *m_normalLength, 1.0f));

			Aim(hit.distance, normalEnd);
		};

	private:
		// The beam, straight ahead to distance in the laser's own space.
		static std::vector<float> Beam(float distance)
		{
			return { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -distance };
		};

		void Aim(float distance)
		{
			m_mesh->m_vertices = Beam(distance);
		};

		// With the normal from the beam's end out to normalEnd.
		void Aim(float distance, const glm::vec3& normalEnd)
		{
			std::vector<float> lines = Beam(distance);
			lines.insert(lines.end(), { 0.0f, 0.0f, -distance, normalEnd.x, normalEnd.y, normalEnd.z });

			m_mesh->m_vertices = lines;
		};

		void Untint()
		{
			if (m_tinted)
				m_tinted->color = m_untinted;

			m_tinted = nullptr;
		};

		// Degrees a frame.
		Loom::Serial<float> m_speed = 0.4f;
		Loom::Serial<float> m_range = 12.0f;
		Loom::Serial<float> m_normalLength = 0.8f;
		Loom::Serial<glm::vec3> m_beamColor = glm::vec3(1.0f, 0.2f, 0.2f);
		Loom::Serial<glm::vec3> m_hitColor = glm::vec3(1.0f, 0.85f, 0.2f);
		Loom::Serial<std::string> m_shader = "Assets/Beam.shader";

		Loom::Mesh* m_mesh = nullptr;
		Loom::Material* m_tinted = nullptr;
		glm::vec3 m_untinted{ 1.0f };
	};
};
