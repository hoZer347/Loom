#pragma once

#include "glm/glm.hpp"

#include <vector>


namespace Loom
{
	struct Collider;
	struct MeshCollider;

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
		constexpr int AXES = 3;

		struct Sphere
		{
			glm::vec3 center{ 0.0f };
			float radius = 0.0f;
		};

		// Its axes are unit length; half is how far it reaches along each.
		struct Box
		{
			glm::vec3 center{ 0.0f };
			glm::vec3 axes[AXES];
			glm::vec3 half{ 0.0f };
		};

		// A collider as it stands in the world. A mesh is its collider, whose
		// triangles Pose has put where world has them.
		struct Shape
		{
			enum Kind { NONE, SPHERE, BOX, MESH } kind = NONE;

			Sphere sphere{ };
			Box box{ };
			const MeshCollider* mesh = nullptr;
		};

		Shape Pose(const Collider& collider, const glm::mat4& world);

		// Where a ray with a normalised direction first meets a posed box or
		// sphere from outside, within maxDistance: false for a mesh, which
		// its collider tests itself, and for one the ray starts inside.
		bool Raycast(const Shape& shape, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float& distance, glm::vec3& normal);

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
		void Collide(const Shape& a, const Shape& b, float margin, std::vector<Manifold>& manifolds);
	};
};
