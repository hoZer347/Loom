#pragma once

#include "Loom API.h"

#include "Component.h"

#include <vector>

namespace Loom
{
	struct Material;

	struct LOOM_API Mesh : Component<Mesh>
	{
		Mesh(uint32_t primitive);
		~Mesh();

		void OnRender() override;
		void OnGui() override;

		Serial<Material*> material;
		Serial<uint32_t> primitive_id;
		Serial<uint32_t> m_draw_type = 0x88E4;	// GL_STATIC_DRAW
		Serial<std::vector<float>> m_vertices;
		std::vector<uint32_t> m_indices;
	};
};
