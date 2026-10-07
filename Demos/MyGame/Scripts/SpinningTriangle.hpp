#pragma once

#include "Component.h"
#include "Material.h"
#include "Mesh.h"

#include <cmath>
#include <numbers>
#include <string>


namespace MyGameScripts
{
	// A script is an ordinary Loom component, written whole in one header.
	// Deriving from Loom::Component is all it takes to appear under Add
	// Component. Serial members show up in the inspector and are written to the
	// scene file, with no editor code to write for them.
	struct SpinningTriangle : Loom::Component<SpinningTriangle>
	{
		void OnAttach() override
		{
			Loom::Material* material = m_gameObject->GetComponent<Loom::Material>();

			if (material == nullptr)
				material = m_gameObject->Attach<Loom::Material>();

			// Named, not built here: the engine compiles one shader per path and
			// shares it, so a script hands over the name and lets it do that.
			material->SetShaderPath(m_shader);

			m_mesh = m_gameObject->GetComponent<Loom::Mesh>();

			if (m_mesh == nullptr)
				m_mesh = m_gameObject->Attach<Loom::Mesh>();

			m_mesh->material = material;

			Rebuild();
		};

		void OnUpdate() override
		{
			if (!m_spinning)
				return;

			m_angle += m_speed;

			Rebuild();
		};

	private:
		static constexpr int corners = 3;

		void Rebuild()
		{
			if (m_mesh == nullptr)
				return;

			m_mesh->m_vertices->clear();

			for (int corner = 0; corner < corners; corner++)
			{
				const float turn = m_angle + (float)corner * 2.0f * std::numbers::pi_v<float> / corners;

				m_mesh->m_vertices->push_back(std::cos(turn) * m_radius);
				m_mesh->m_vertices->push_back(std::sin(turn) * m_radius);
				m_mesh->m_vertices->push_back(0.0f);
			};
		};

		LOOM_SERIAL(float, m_speed, 0.02f);
		LOOM_SERIAL(float, m_radius, 0.6f);
		LOOM_SERIAL(float, m_angle);
		LOOM_SERIAL(bool, m_spinning, true);
		LOOM_SERIAL(std::string, m_shader, "Assets/Shader.shader");

		Loom::Mesh* m_mesh = nullptr;
	};
};
