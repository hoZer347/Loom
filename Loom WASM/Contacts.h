#pragma once

#include "glm/glm.hpp"

#include <vector>


namespace Loom
{
	struct Collider;

	/**
	* Loom::Contacts
	* - Where two colliders touch, posed by their world matrices: spheres and
	*   boxes against each other, and either against a MeshCollider's
	*   triangles. Two MeshColliders never touch
	* - Boxes are tested on their separating axes, the deepest face clipped
	*   against the other's to give up to four points, as most engines do
	* - Shapes still apart by up to margin are reported too, with a negative
	*   depth, so a solver can stop them closing faster than the gap allows
	*   rather than letting a fast one pass through in a single step
	*/
	namespace Contacts
	{
		constexpr int MOST_POINTS = 4;

		// The points of one touching surface, with the normal pointing out of
		// the first collider into the second. A depth below zero is a gap.
		struct Manifold
		{
			glm::vec3 normal{ 0.0f };
			int count = 0;
			glm::vec3 points[MOST_POINTS];
			float depths[MOST_POINTS] = { };
		};

		// Appends a manifold for each surface where a touches b, or comes
		// within margin of it; a mesh gives one for each of its triangles.
		void Collide(
			const Collider& a, const glm::mat4& worldA,
			const Collider& b, const glm::mat4& worldB,
			float margin,
			std::vector<Manifold>& manifolds);
	};
};
