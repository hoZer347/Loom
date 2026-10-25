#include "EditorViewport.h"

#include "Renderer.h"


namespace Loom
{
	EditorViewport::~EditorViewport()
	{
		Destroy();
	};

	void EditorViewport::Destroy()
	{
		Renderer* renderer = Renderer::Get();

		for (uint32_t* texture : { &m_texture, &m_result })
		{
			if (renderer && *texture)
				renderer->DestroyTexture(*texture);

			*texture = 0;
		};

		m_shown = 0;
		m_width = m_height = 0;
	};

	void EditorViewport::Resize(int width, int height)
	{
		if (m_texture && width == m_width && height == m_height)
			return;

		Destroy();

		m_width = width;
		m_height = height;
		m_texture = Renderer::Get()->CreateColorTarget(m_width, m_height);
	};

	void EditorViewport::Render(
		int width,
		int height,
		const float* clear_colour,
		const std::function<void()>& draw,
		const std::function<void()>& over)
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer || width <= 0 || height <= 0)
			return;

		Resize(width, height);

		if (!m_texture)
			return;

		renderer->PushTarget(m_texture);
		renderer->Clear(clear_colour, true);

		m_pass.Begin();
		draw();
		const bool passes = m_pass.End();

		if (passes && !m_result)
			m_result = renderer->CreateColorTarget(m_width, m_height);

		m_shown = passes && m_result ? m_result : m_texture;

		if (m_shown != m_texture)
		{
			renderer->PopTarget();
			renderer->PushTarget(m_result);
			m_pass.Draw(m_texture, { m_width, m_height });
		};

		if (over)
			over();

		renderer->PopTarget();
	};

	void* EditorViewport::GetTextureID() const
	{
		return Renderer::Get()->ImGuiTexture(m_shown);
	};
};
