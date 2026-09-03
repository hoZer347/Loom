#pragma once

#include <cstdint>


namespace Loom
{
	struct Scene;

	/**
	* Loom::EditorViewport
	* - An off-screen colour + depth target a scene is rendered into
	* - Lets the editor show a live scene as an image in the middle of its dock
	*   space instead of drawing it over the whole window
	*/
	struct EditorViewport final
	{
		EditorViewport() = default;
		~EditorViewport();

		EditorViewport(const EditorViewport&) = delete;
		EditorViewport& operator=(const EditorViewport&) = delete;

		// Renders the scene at the requested size, reallocating the target if the
		// size changed. Safe to call while an ImGui frame is being built: the two
		// pieces of GL state it touches (framebuffer binding, viewport) are saved
		// and restored, and ImGui only records the texture id until it draws.
		void Render(Scene* scene, int width, int height);

		// The colour attachment, as an ImTextureID.
		void* GetTextureID() const;

		uint32_t GetTexture() const { return m_texture; };
		int GetWidth() const { return m_width; };
		int GetHeight() const { return m_height; };

	private:
		void Resize(int width, int height);
		void Destroy();

		uint32_t m_fbo = 0;
		uint32_t m_texture = 0;
		uint32_t m_depth = 0;

		int m_width = 0;
		int m_height = 0;
	};
};
