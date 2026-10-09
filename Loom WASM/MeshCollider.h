#pragma once

#include "Loom API.h"

#include "Collider.h"

#include "glm/glm.hpp"

#include <cstdint>
#include <vector>


namespace Loom
{
	/**
	* Loom::MeshCollider
	* - The triangles of the Mesh on the same GameObject, drawn as triangles,
	*   strips or fans, through its indices when it has them; a mesh of points
	*   or lines has none to hit, and neither does a GameObject without a Mesh
	* - Hit from either side, as Loom draws a mesh from either side, with the
	*   normal turned back toward the ray. So unlike Unity's, a ray starting
	*   inside a closed mesh hits it on the way out
	* - Keeps its own tree of the triangles, rebuilt when the mesh's vertices,
	*   indices or primitive have changed since it last looked: at every
	*   physics step, and when asked for a raycast, its bounds or its
	*   wireframe directly
	*/
	struct LOOM_API MeshCollider : Component<MeshCollider, "Physics", Collider>
	{
		size_t TriangleCount() const { return m_triangles.size(); };

		bool IsTrigger() const override { return isTrigger; };
		ColliderShape Shape() const override { return ColliderShape::Mesh; };

		// The triangles that may reach into near, a box in world space, posed
		// by world: three corners each, in world space.
		void TrianglesNear(const glm::mat4& world, const Bounds& near, std::vector<glm::vec3>& corners) const;

		Serial<bool> isTrigger;

	protected:
		bool Refresh() const override;

		Bounds BoundsIn(const glm::mat4& world) const override;
		void WireframeIn(const glm::mat4& world, std::vector<glm::vec3>& lines) const override;
		bool Intersect(const glm::mat4& world, const Ray& ray, float maxDistance, RaycastHit& hit) const override;

	private:
		struct Triangle
		{
			glm::vec3 corners[3];

			glm::vec3 Centre() const;
		};

		// Laid out depth first: a branch's first child follows it, and second
		// says where its other one is.
		struct Node
		{
			Bounds bounds;

			// A leaf's triangles; count is 0 for a branch.
			uint32_t first = 0;
			uint32_t count = 0;

			uint32_t second = 0;
		};

		void Rebuild() const;
		void Build(uint32_t first, uint32_t count) const;

		// What the tree was built from, to tell when the mesh has changed.
		mutable std::vector<float> m_source{ };
		mutable std::vector<uint32_t> m_indices{ };
		mutable uint32_t m_primitive = 0;

		mutable std::vector<Triangle> m_triangles{ };
		mutable std::vector<Node> m_nodes{ };
	};
};
