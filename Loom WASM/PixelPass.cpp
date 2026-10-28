#include "PixelPass.h"

#include "Mesh.h"
#include "Renderer.h"
#include "Scene.h"

#include <algorithm>
#include <iterator>


namespace Loom
{
	namespace
	{
		constexpr uint32_t STATIC_DRAW = 0x88E4;	// GL_STATIC_DRAW

		// Two triangles over the whole of what is drawn into.
		constexpr float QUAD[] =
		{
			-1.0f, -1.0f, 0.0f,
			1.0f, -1.0f, 0.0f,
			1.0f, 1.0f, 0.0f,
			-1.0f, -1.0f, 0.0f,
			1.0f, 1.0f, 0.0f,
			-1.0f, 1.0f, 0.0f,
		};
		constexpr size_t FLOATS_PER_VERTEX = 3;
		constexpr size_t QUAD_VERTICES = std::size(QUAD) / FLOATS_PER_VERTEX;

		constexpr const char* COPY_PROGRAM = "Pixel pass copy";

		constexpr const char* COPY_VERTEX = R"(
layout(location = 0) in vec3 aPos;

out vec2 loom_ScreenUV;

const float LOOM_UV_PER_NDC = 0.5;

void main()
{
	loom_ScreenUV = aPos.xy * LOOM_UV_PER_NDC + LOOM_UV_PER_NDC;
	gl_Position = vec4(aPos.xy, 0.0, 1.0);
}
)";

		constexpr const char* COPY_FRAGMENT = R"(
uniform sampler2D u_scene;

in vec2 loom_ScreenUV;

out vec4 FragColor;

void main()
{
	FragColor = vec4(texture(u_scene, loom_ScreenUV).rgb, 1.0);
}
)";
	};

	PixelPass::~PixelPass()
	{
		Release();
	};

	#pragma region Noting

	void PixelPass::Begin()
	{
		m_noted.clear();

		if (s_noting != this)
			m_previous = s_noting;

		s_noting = this;
	};

	bool PixelPass::End()
	{
		if (s_noting == this)
			s_noting = m_previous;

		m_previous = nullptr;

		return !m_noted.empty();
	};

	void PixelPass::Note(uint32_t program)
	{
		if (!s_noting || !program || std::ranges::find(s_noting->m_noted, program) != s_noting->m_noted.end())
			return;

		// Now, while the view being drawn is the one the pass runs over.
		s_noting->m_noted.push_back(program);
		Scene::ApplyView(program);
	};

	#pragma endregion

	#pragma region Drawing

	void PixelPass::Draw(uint32_t target, glm::ivec2 size)
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer || !target)
			return;

		if (m_noted.empty())
		{
			if (!m_copy)
				m_copy = renderer->CreateProgram(COPY_PROGRAM, COPY_VERTEX, COPY_FRAGMENT);

			renderer->SetTexture(m_copy, "u_scene", target);
			renderer->Clear(nullptr, true);
			renderer->Draw(m_copy, Mesh::Triangles, QUAD, QUAD_VERTICES, STATIC_DRAW, VertexFormat::Position);
			return;
		};

		const uint32_t depth = renderer->TargetDepth(target);
		uint32_t source = target;

		for (size_t i = 0; i < m_noted.size(); i++)
		{
			const bool last = i + 1 == m_noted.size();
			const uint32_t into = last ? 0 : Intermediate(i % std::size(m_intermediates), size);

			if (!last && !into)
				return;

			if (into)
				renderer->PushTarget(into);

			renderer->Clear(nullptr, true);
			renderer->SetTexture(m_noted[i], "u_scene", source);
			renderer->SetTexture(m_noted[i], "u_sceneDepth", depth);
			renderer->Draw(m_noted[i], Mesh::Triangles, QUAD, QUAD_VERTICES, STATIC_DRAW, VertexFormat::Position);

			if (into)
				renderer->PopTarget();

			source = into;
		};
	};

	uint32_t PixelPass::Intermediate(size_t index, glm::ivec2 size)
	{
		Renderer* renderer = Renderer::Get();

		if (size != m_size)
		{
			for (uint32_t& intermediate : m_intermediates)
			{
				if (intermediate)
					renderer->DestroyTexture(intermediate);

				intermediate = 0;
			};

			m_size = size;
		};

		if (!m_intermediates[index])
			m_intermediates[index] = renderer->CreateColorTarget(size.x, size.y);

		return m_intermediates[index];
	};

	void PixelPass::Release()
	{
		End();

		Renderer* renderer = Renderer::Get();

		for (uint32_t& intermediate : m_intermediates)
		{
			if (renderer && intermediate)
				renderer->DestroyTexture(intermediate);

			intermediate = 0;
		};

		if (renderer && m_copy)
			renderer->DeleteProgram(m_copy);

		m_copy = 0;
		m_size = glm::ivec2(0);
		m_noted.clear();
	};

	#pragma endregion
};
