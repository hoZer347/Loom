#pragma once

#include "Loom API.h"

#include "Component.h"

#include <vector>

namespace Loom
{
	struct Material;

	struct LOOM_API Mesh : Component<Mesh>
	{
		// The GL values, spelled out so this header does not have to pull in GL.
		enum Primitive : uint32_t
		{
			Points = 0x0000,
			Lines = 0x0001,
			LineLoop = 0x0002,
			LineStrip = 0x0003,
			Triangles = 0x0004,
			TriangleStrip = 0x0005,
			TriangleFan = 0x0006,
		};

		enum DrawType : uint32_t
		{
			Stream = 0x88E0,
			Static = 0x88E4,
			Dynamic = 0x88E8,
		};

		Mesh(Primitive primitive = Triangles);
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

		Serial<Material*> material;
		Serial<Primitive> primitive_id;
		Serial<DrawType> m_draw_type = Static;
		Serial<std::vector<float>> m_vertices;
		std::vector<uint32_t> m_indices;
	};
};
