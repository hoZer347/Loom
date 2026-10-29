#pragma once

#include "Loom API.h"

#include "glm/glm.hpp"

#include <cstdint>
#include <iterator>
#include <vector>


namespace Loom
{
	/**
	* Loom::PixelPass
	* - Runs the pixel shaders noted while a view was drawn over the finished
	*   view, once for each pixel, one after another, each reading what the
	*   one before it left
	* - A view drawn with none of them is left as it is
	*/
	struct LOOM_API PixelPass final
	{
		PixelPass() = default;
		~PixelPass();

		PixelPass(const PixelPass&) = delete;
		PixelPass& operator=(const PixelPass&) = delete;

		#pragma region Noting

		// Notes each pixel shader drawn until End, in place of any pass noting
		// already.
		void Begin();

		// Stops noting, hands noting back to the pass it took over from, and
		// answers whether any pass was noted.
		bool End();

		// Called by each PixelShader drawn, with its program.
		static void Note(uint32_t program);

		#pragma endregion

		#pragma region Drawing

		// Draws a colour target of size through the passes noted, or as it is
		// when none were, into whatever is being drawn into, clearing its depth.
		void Draw(uint32_t target, glm::ivec2 size);

		// Gives back what it holds, for before the renderer goes.
		void Release();

		// Two triangles over the whole of what is drawn into.
		static constexpr float Quad[] =
		{
			-1.0f, -1.0f, 0.0f,
			1.0f, -1.0f, 0.0f,
			1.0f, 1.0f, 0.0f,
			-1.0f, -1.0f, 0.0f,
			1.0f, 1.0f, 0.0f,
			-1.0f, 1.0f, 0.0f,
		};
		static constexpr size_t FloatsPerVertex = 3;
		static constexpr size_t QuadVertices = std::size(Quad) / FloatsPerVertex;

		#pragma endregion

	private:
		#pragma region Drawing

		uint32_t Intermediate(size_t index, glm::ivec2 size);

		#pragma endregion

		// Each pass's program, in the order its first material was drawn.
		std::vector<uint32_t> m_noted;

		// What all but the last pass draw into, in turn, each reading the one before.
		static constexpr size_t INTERMEDIATES = 2;
		uint32_t m_intermediates[INTERMEDIATES]{ };
		glm::ivec2 m_size{ 0 };

		// Draws a texture as it is, for a view with no passes.
		uint32_t m_copy = 0;

		PixelPass* m_previous = nullptr;

		static inline PixelPass* s_noting = nullptr;
	};
};
