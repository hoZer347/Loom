#pragma once

#include "Loom API.h"

#include "glm/glm.hpp"

#include <limits>
#include <vector>


namespace Loom
{
	struct Collider;
	struct GameObject;

	#pragma region Rays

	constexpr float INFINITE_DISTANCE = std::numeric_limits<float>::infinity();

	/**
	* Loom::Ray
	* - A half-line from origin along direction. Raycasts normalise direction
	*   themselves, so distances along a ray are always world units
	*/
	struct Ray final
	{
		glm::vec3 origin{ 0.0f };
		glm::vec3 direction{ 0.0f, 0.0f, -1.0f };

		glm::vec3 GetPoint(float distance) const { return origin + direction * distance; };
	};

	/**
	* Loom::RaycastHit
	* - Where a ray first met a collider: the point on its surface, the
	*   surface's outward normal there, and how far along the ray that was
	*/
	struct RaycastHit final
	{
		Collider* collider = nullptr;
		GameObject* gameObject = nullptr;

		glm::vec3 point{ 0.0f };
		glm::vec3 normal{ 0.0f };
		float distance = 0.0f;
	};

	#pragma endregion

	#pragma region Bounds

	/**
	* Loom::Bounds
	* - An axis-aligned box from min to max, in whatever space its user is in:
	*   world space in the collider tree, a collider's own space in BoxCollider
	*/
	struct LOOM_API Bounds final
	{
		glm::vec3 min{ 0.0f };
		glm::vec3 max{ 0.0f };

		glm::vec3 Center() const { return (min + max) * 0.5f; };
		glm::vec3 Extents() const { return (max - min) * 0.5f; };

		// These are asked millions of times a second by the collider tree, so
		// are inline, and compare all six sides at once rather than branch on
		// each.

		// Half the surface area, which is all the tree's cost estimates compare.
		float HalfArea() const
		{
			const glm::vec3 size = max - min;

			return size.x * size.y + size.y * size.z + size.z * size.x;
		};

		bool Contains(const Bounds& inner) const
		{
			return (min.x <= inner.min.x) & (min.y <= inner.min.y) & (min.z <= inner.min.z)
				& (inner.max.x <= max.x) & (inner.max.y <= max.y) & (inner.max.z <= max.z);
		};

		bool Overlaps(const Bounds& other) const
		{
			return (min.x <= other.max.x) & (min.y <= other.max.y) & (min.z <= other.max.z)
				& (other.min.x <= max.x) & (other.min.y <= max.y) & (other.min.z <= max.z);
		};

		// Parenthesised, past windows.h's min and max macros.
		Bounds Union(const Bounds& other) const { return { (glm::min)(min, other.min), (glm::max)(max, other.max) }; };
		Bounds Expanded(float margin) const { return { min - glm::vec3(margin), max + glm::vec3(margin) }; };

		// The stretch of the ray's parameter that lies inside the box, entry
		// negative when the origin is inside, and the axis the ray enters
		// through. False when the ray's line misses the box.
		bool IntersectRay(const Ray& ray, float& entry, float& exit, int& entryAxis) const;
	};

	#pragma endregion

	/**
	* Loom::Physics
	* - Raycasts against every open scene, as Unity's Physics.Raycast does. A
	*   scene's own Scene::Raycast keeps to that scene
	* - The engine steps every scene's physics at a fixed rate, as Unity's
	*   FixedUpdate does: as many steps of FixedDeltaTime as the frame's time
	*   covers, up to a few sixtieths of a second's worth, each after its
	*   OnFixedUpdate and before OnUpdate. The last of them is solved on a
	*   thread of its own while the GUI is drawn. Scene::Physics takes one
	*   step by hand
	* - A collider is where the scene's last physics step left it, so one
	*   moved in OnUpdate is found where it went after the next step
	* - Any thread may raycast, or ask a collider where it is, while others do
	* - A ray that starts inside a box or sphere does not hit it
	*/
	namespace Physics
	{
		#pragma region Settings

		LOOM_API glm::vec3 GetGravity();
		LOOM_API void SetGravity(const glm::vec3& gravity);

		// Seconds a physics step covers. One that is not above zero and finite
		// is refused.
		LOOM_API float FixedDeltaTime();
		LOOM_API void SetFixedDeltaTime(float seconds);

		// The whole steps seconds covers, taken off it, within what keeps a slow
		// frame from leaving the next further behind. No more than a step is left.
		LOOM_API int TakeSteps(float& seconds);

		#pragma endregion

		#pragma region Raycasts

		// The nearest hit within maxDistance, into hit.
		LOOM_API bool Raycast(const Ray& ray, RaycastHit& hit, float maxDistance = INFINITE_DISTANCE);
		LOOM_API bool Raycast(const glm::vec3& origin, const glm::vec3& direction, RaycastHit& hit, float maxDistance = INFINITE_DISTANCE);

		// Every hit within maxDistance, nearest first.
		LOOM_API std::vector<RaycastHit> RaycastAll(const Ray& ray, float maxDistance = INFINITE_DISTANCE);
		LOOM_API std::vector<RaycastHit> RaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance = INFINITE_DISTANCE);

		#pragma endregion
	};
};
