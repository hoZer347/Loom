#pragma once

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
		EditorViewport() = default;
		~EditorViewport();

		EditorViewport(const EditorViewport&) = delete;
		EditorViewport& operator=(const EditorViewport&) = delete;

		// Runs draw at the requested size with the target bound and cleared to
		// clear_colour (RGBA), reallocating the target if the size changed.
		// Safe to call while an ImGui frame is being built: the target is pushed
		// and popped around draw, and ImGui only records the texture id until
		// it draws.
		void Render(int width, int height, const float* clear_colour, const std::function<void()>& draw);

		// The colour attachment, as an ImTextureID.
		void* GetTextureID() const;

		uint32_t GetTexture() const { return m_texture; };
		int GetWidth() const { return m_width; };
		int GetHeight() const { return m_height; };

	private:
		void Resize(int width, int height);
		void Destroy();

		uint32_t m_texture = 0;

		int m_width = 0;
		int m_height = 0;
	};
};
