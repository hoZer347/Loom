#pragma once

#include "Component.h"
#include "OpenGL.h"

#include <vector>


namespace Loom
{
	struct Material;

	struct Mesh : Component<Mesh>
	{
		Mesh(uint32_t primitive_id);
		~Mesh();

		void OnRender() override;

		Material* material = nullptr;
		uint32_t primitive_id;
		std::vector<float> m_vertices;
		std::vector<uint32_t> m_indices;
	};
};
