#include "ColliderGizmos.h"

#include "Camera.h"
#include "Collider.h"
#include "GameObject.h"
#include "OpenGL.h"
#include "Renderer.h"

#include <iostream>
#include <stdexcept>


namespace Loom
{
	namespace
	{
		constexpr glm::vec4 collider_colour(0.55f, 0.95f, 0.5f, 1.0f);

		const char* const line_vertex = R"(
layout(location = 0) in vec3 aPos;

uniform mat4 u_viewProjection;

void main()
{
	gl_Position = u_viewProjection * vec4(aPos, 1.0);
}
)";

		const char* const line_fragment = R"(
uniform vec4 u_color;

out vec4 FragColor;

void main()
{
	FragColor = u_color;
}
)";
	};

	ColliderGizmos::~ColliderGizmos()
	{
		Renderer* renderer = Renderer::Get();

		if (renderer && m_program)
			renderer->DeleteProgram(m_program);
	};

	bool ColliderGizmos::Compile()
	{
		if (m_compileTried)
			return m_program != 0;

		m_compileTried = true;

		try
		{
			m_program = Renderer::Get()->CreateProgram("Loom Editor Collider Gizmos", line_vertex, line_fragment);
		}
		catch (const std::exception& e)
		{
			std::cerr << "Collider gizmos: " << e.what() << std::endl;
		};

		return m_program != 0;
	};

	void ColliderGizmos::Gather(GameObject& gameObject)
	{
		if (gameObject.IsDisabled())
			return;

		for (ComponentBase* component : gameObject.GetComponents())
			if (const Collider* collider = dynamic_cast<const Collider*>(component))
			{
				const std::vector<glm::vec3> lines = collider->Wireframe();
				m_lines.insert(m_lines.end(), lines.begin(), lines.end());
			};

		for (GameObject* child : gameObject.GetChildren())
			Gather(*child);
	};

	void ColliderGizmos::Draw(GameObject& selected, const Camera& camera)
	{
		m_lines.clear();
		Gather(selected);

		Renderer* renderer = Renderer::Get();

		if (m_lines.empty() || !renderer || !Compile())
			return;

		renderer->Clear(nullptr, true);

		camera.Apply(m_program);
		renderer->SetUniform(m_program, "u_color", collider_colour);
		renderer->Draw(m_program, GL_LINES, &m_lines[0].x, m_lines.size(), GL_STREAM_DRAW);
	};
};
