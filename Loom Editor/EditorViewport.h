#pragma once

#include "PixelPass.h"

#include <cstdint>
#include <functional>


namespace Loom
{
	/**
	* Loom::EditorViewport
	* - An off-screen colour + depth target the open scenes are rendered into
	* - Lets the editor show a live scene as an image in the middle of its dock
	*   space instead of drawing it over the whole window
	*/
	struct EditorViewport final
	{
		// A view whose shaders' pixel passes would run over more than the scene,
		// such as an editing overlay, leaves them out.
		explicit EditorViewport(bool pixel_passes = true) : m_pixelPasses(pixel_passes) { };
		~EditorViewport();

		EditorViewport(const EditorViewport&) = delete;
		EditorViewport& operator=(const EditorViewport&) = delete;

		// Runs draw at the requested size with the target bound and cleared to
		// clear_colour (RGBA), reallocating the target if the size changed, then
		// the pixel passes of what it drew, then over on top of them.
		// Safe to call while an ImGui frame is being built: the target is pushed
		// and popped around draw, and ImGui only records the texture id until
		// it draws.
		void Render(
			int width,
			int height,
			const float* clear_colour,
			const std::function<void()>& draw,
			const std::function<void()>& over = { });

		// What the last Render left, as an ImTextureID.
		void* GetTextureID() const;

		uint32_t GetTexture() const { return m_shown; };
		int GetWidth() const { return m_width; };
		int GetHeight() const { return m_height; };

	private:
		void Resize(int width, int height);
		void Destroy();

		uint32_t m_texture = 0;

		// What the pixel passes draw into, made the first time there are any.
		uint32_t m_result = 0;
		uint32_t m_shown = 0;
		PixelPass m_pass;
		bool m_pixelPasses;

		int m_width = 0;
		int m_height = 0;
	};
};
