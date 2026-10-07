#include "EditorViewport.h"

#include "Engine.h"
#include "Renderer.h"
#include "Scene.h"


namespace Loom
{
	EditorViewport::~EditorViewport()
	{
		Destroy();
	};

	void EditorViewport::Destroy()
	{
		Renderer* renderer = Renderer::Get();

		if (renderer && m_texture)
			renderer->DestroyTexture(m_texture);

		m_texture = 0;
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

	void EditorViewport::Render(int width, int height)
	{
		Render(
			width,
			height,
			Engine::clearColor,
			[]()
			{
				for (Scene* scene : Scene::GetScenes())
					scene->Render();
			});
	};

	void EditorViewport::Render(int width, int height, const float* clear_colour, const std::function<void()>& draw)
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer || width <= 0 || height <= 0)
			return;

		Resize(width, height);

		if (!m_texture)
			return;

		renderer->PushTarget(m_texture);
		renderer->Clear(clear_colour, true);

		draw();

		renderer->PopTarget();
	};

	void* EditorViewport::GetTextureID() const
	{
		return Renderer::Get()->ImGuiTexture(m_texture);
	};
};
