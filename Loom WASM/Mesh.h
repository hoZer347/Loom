#pragma once

#include "Loom API.h"

#include "Component.h"
#include "ShaderVariables.h"

#include <vector>

namespace Loom
{
	struct Material;
	struct Renderer;

	struct LOOM_API Mesh : Component<Mesh, "Rendering">
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

		// Draws the vertices with the given program, under the GameObject's
		// world matrix (as u_model), which is how a pass that is not the
		// material's (the shadow pass) draws the same geometry. A shader that
		// declares no u_model draws the vertices untransformed. A Skin on the
		// GameObject bends them first, and m_textureCoordinates go with them as
		// aUV and m_normals as aNormal. The renderer keeps them between draws,
		// and they are handed over again only once one of those fields has
		// changed, or the Skin has bent them again.
		void Draw(uint32_t program);

		// Draws the meshes farthest from Camera::current first, writing no
		// depth, so each blends over everything behind it.
		static void DrawTransparent(const std::vector<Mesh*>& meshes);

		// While Scene::Render points this at a list, a mesh whose shader has
		// transparency on goes onto it rather than being drawn, so it can be
		// drawn after everything opaque.
		static inline std::vector<Mesh*>* transparent = nullptr;

		void OnGui() override;

		Serial<Material*> material;
		Serial<Primitive> primitive_id;
		Serial<DrawType> m_draw_type = Static;
		Serial<std::vector<float>> m_vertices;

		// This mesh's values for its material's per-instance shader variables.
		Serial<UniformValues> variables;

		// Two floats a vertex, drawn as aUV, when there is a pair for every
		// vertex. Last, so the fields before it keep their places in scenes
		// saved without it.
		Serial<std::vector<float>> m_textureCoordinates;

		// Three floats a vertex, drawn as aNormal, when there are three for
		// every vertex; shading lights each face flat without them. After
		// m_textureCoordinates for the same reason.
		Serial<std::vector<float>> m_normals;

		// Leaves a generated mesh's geometry out of what is written.
		bool IsSaved(const SerializedField& field) const override;

		std::vector<uint32_t> m_indices;

		// On, the vertices, texture coordinates and normals are made by a
		// script as it runs, such as ground grown from a seed: never written
		// to a scene or prefab, so neither saved nor compared with a prefab's.
		// Not saved itself, since the script sets it again as it makes them
		// again, once the mesh is read back.
		bool m_generated = false;

		// Drawn in place of m_vertices while it holds as many floats. What a
		// Skin bends the vertices into; never saved.
		std::vector<float> m_deformed;

		// The same for m_normals.
		std::vector<float> m_deformedNormals;

	private:
		// The frame m_deformed was bent for, so the passes after the first in
		// a frame draw what it bent.
		unsigned long long m_deformedFrame = 0;

		// What the renderer was last handed, by the versions of the fields it
		// was made from, and whether the Skin bent them and for which frame.
		struct Handed
		{
			unsigned long long vertices = 0;
			unsigned long long textureCoordinates = 0;
			unsigned long long normals = 0;
			unsigned long long drawType = 0;
			bool deformed = false;
			unsigned long long deformedFrame = 0;

			bool operator==(const Handed&) const = default;
		};

		// The vertices as the renderer keeps them, which renderer that is, and
		// what it was handed.
		uint32_t m_kept = 0;
		Renderer* m_keeper = nullptr;
		Handed m_handed;
	};
};
