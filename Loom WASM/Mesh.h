#pragma once

#include "Loom API.h"

#include "Component.h"

#include <vector>

namespace Loom
{
	struct Material;

	struct LOOM_API Mesh : Component<Mesh>
	{
		// GL_TRIANGLES, spelled out so this header does not have to pull in GL
		static constexpr uint32_t triangles = 0x0004;

		Mesh(uint32_t primitive = triangles);
		~Mesh();

		// Whether there is a material with a shader to draw with, finding the
		// GameObject's material the first time it is asked. Every pass checks
		// this, so a mesh nothing can draw is invisible to all of them.
		bool IsDrawable();

		void OnRender() override;

		// Uploads the vertices and the GameObject's world matrix (as u_model) and
		// draws them with the given program, which is how a pass that is not the
		// material's (the shadow pass) draws the same geometry. A shader that
		// declares no u_model draws the vertices untransformed.
		void Draw(uint32_t program);

		void OnGui() override;

		LOOM_SERIAL(Material*, material);
		LOOM_SERIAL(uint32_t, primitive_id);
		LOOM_SERIAL(uint32_t, m_draw_type, 0x88E4);	// GL_STATIC_DRAW
		LOOM_SERIAL(std::vector<float>, m_vertices);
		std::vector<uint32_t> m_indices;
	};
};
