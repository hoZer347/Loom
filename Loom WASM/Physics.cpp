#include "Physics.h"

#include "Scene.h"

#include <algorithm>
#include <cmath>


namespace Loom
{
	bool Bounds::IntersectRay(const Ray& ray, float& entry, float& exit, int& entryAxis) const
	{
		entry = -INFINITE_DISTANCE;
		exit = INFINITE_DISTANCE;
		entryAxis = -1;

		for (int axis = 0; axis < glm::vec3::length(); axis++)
		{
			const float origin = ray.origin[axis];
			const float direction = ray.direction[axis];

			// Parallel to this pair of planes: dividing would make 0 * inf of a
			// ray lying in one of them.
			if (direction == 0.0f)
			{
				if (origin < min[axis] || origin > max[axis])
					return false;

				continue;
			};

			float near = (min[axis] - origin) / direction;
			float far = (max[axis] - origin) / direction;

			if (near > far)
				std::swap(near, far);

			if (near > entry)
			{
				entry = near;
				entryAxis = axis;
			};

			exit = std::min(exit, far);

			if (entry > exit)
				return false;
		};

		return true;
	};

	namespace Physics
	{
		namespace
		{
			// Unity's defaults, but for the step, which is the display's usual rate.
			glm::vec3 s_gravity(0.0f, -9.81f, 0.0f);
			float s_fixedDeltaTime = 1.0f / 60.0f;
		};

		glm::vec3 GetGravity() { return s_gravity; };
		void SetGravity(const glm::vec3& gravity) { s_gravity = gravity; };

		float FixedDeltaTime() { return s_fixedDeltaTime; };
		void SetFixedDeltaTime(float seconds)
		{
			if (seconds > 0.0f && std::isfinite(seconds))
				s_fixedDeltaTime = seconds;
		};

		bool Raycast(const Ray& ray, RaycastHit& hit, float maxDistance)
		{
			bool found = false;

			for (Scene* scene : Scene::GetScenes())
			{
				RaycastHit sceneHit;

				if (scene->Raycast(ray, sceneHit, maxDistance))
				{
					hit = sceneHit;
					maxDistance = sceneHit.distance;
					found = true;
				};
			};

			return found;
		};

		bool Raycast(const glm::vec3& origin, const glm::vec3& direction, RaycastHit& hit, float maxDistance)
		{
			return Raycast(Ray{ origin, direction }, hit, maxDistance);
		};

		std::vector<RaycastHit> RaycastAll(const Ray& ray, float maxDistance)
		{
			std::vector<RaycastHit> hits{ };

			for (Scene* scene : Scene::GetScenes())
			{
				const std::vector<RaycastHit> sceneHits = scene->RaycastAll(ray, maxDistance);

				hits.insert(hits.end(), sceneHits.begin(), sceneHits.end());
			};

			std::sort(hits.begin(), hits.end(),
				[](const RaycastHit& a, const RaycastHit& b) { return a.distance < b.distance; });

			return hits;
		};

		std::vector<RaycastHit> RaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance)
		{
			return RaycastAll(Ray{ origin, direction }, maxDistance);
		};
	};
};
