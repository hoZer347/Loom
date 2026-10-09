#pragma once

#include "Loom API.h"

#include "Component.h"
#include "ColliderTree.h"
#include "Contacts.h"
#include "Physics.h"

#include "glm/glm.hpp"

#include <vector>


namespace Loom
{
	/**
	* Loom::Collider
	* - A shape on a GameObject that rays can hit, in the GameObject's own
	*   space, so it moves, turns and scales with its transform
	* - Each scene keeps its enabled colliders in a ColliderTree, which every
	*   physics step brings up to date (see Loom::Physics)
	* - GetComponent<Collider> finds whichever kind a GameObject has
	*/
	enum class ColliderShape { Box, Sphere, Mesh };

	struct LOOM_API Collider : ComponentBase
	{
		~Collider();

		// Keeps the scene's tree in step with where the collider is now.
		void OnPhysics() override;

		// The nearest point along the ray, within maxDistance, where it meets
		// this collider, where its transform has it now rather than where the
		// last physics step left it.
		bool Raycast(const Ray& ray, RaycastHit& hit, float maxDistance = INFINITE_DISTANCE);

		// What the collider covers now, as a box in world space.
		Bounds WorldBounds() const;

		// Its outline where it is now, in world space, as a list of lines two
		// points each, for an editor to draw.
		std::vector<glm::vec3> Wireframe() const;

		// A trigger reports what overlaps it, through OnTriggerEnter and the
		// rest, and pushes nothing away.
		virtual bool IsTrigger() const = 0;

		virtual ColliderShape Shape() const = 0;

	protected:
		// Catches up with what the shape is made from, its own fields or
		// another component, before it is synced or asked for anything
		// directly. Whether the shape changed since it last looked.
		virtual bool Refresh() const { return false; };

		virtual Bounds BoundsIn(const glm::mat4& world) const = 0;
		virtual void WireframeIn(const glm::mat4& world, std::vector<glm::vec3>& lines) const = 0;

		// With the ray's direction normalised; fills in point, normal and
		// distance on a hit.
		virtual bool Intersect(const glm::mat4& world, const Ray& ray, float maxDistance, RaycastHit& hit) const = 0;

	private:
		friend struct ColliderTree;
		friend struct Simulation;

		bool RaycastAt(const glm::mat4& world, const Ray& ray, RaycastHit& hit, float maxDistance);

		// Refresh, between syncs: a shape that changed is refitted in its tree,
		// so the tree's raycasts still reach all of it.
		void CatchUp() const;

		ColliderTree* m_tree = nullptr;
		int m_node = ColliderTree::NONE;

		// Where the last sync found it, which the tree's raycasts test against,
		// with the box it covered there and its shape posed there. The tree's
		// Sync and Resize keep all three current.
		glm::mat4 m_synced{ 1.0f };
		mutable Bounds m_bounds{ };

		mutable Contacts::Shape m_shape{ };

		// The body it moves with during a physics step, as the simulation
		// numbers them, or -1 for none.
		int m_body = -1;
	};

	/**
	* Loom::BoxCollider
	* - A box of size around center, hit from outside: a ray starting inside
	*   does not hit it
	* - Under a parent scaled unevenly and turned, which would shear it, it is
	*   taken as the box its own axes make, as Unity's is
	*/
	struct LOOM_API BoxCollider : Component<BoxCollider, "Physics", Collider>
	{
		bool IsTrigger() const override { return isTrigger; };
		ColliderShape Shape() const override { return ColliderShape::Box; };

		Serial<glm::vec3> center;
		Serial<glm::vec3> size = glm::vec3(1.0f);
		Serial<bool> isTrigger;

	protected:
		bool Refresh() const override;

		Bounds BoundsIn(const glm::mat4& world) const override;
		void WireframeIn(const glm::mat4& world, std::vector<glm::vec3>& lines) const override;
		bool Intersect(const glm::mat4& world, const Ray& ray, float maxDistance, RaycastHit& hit) const override;

	private:
		// What center and size were when last refreshed.
		mutable glm::vec3 m_center{ 0.0f };
		mutable glm::vec3 m_size{ 0.0f };
	};

	/**
	* Loom::SphereCollider
	* - A sphere of radius around center. A transform scaled unevenly keeps it
	*   round, sized by its largest scale, as Unity's is
	* - Hit from outside: a ray starting inside does not hit it
	*/
	struct LOOM_API SphereCollider : Component<SphereCollider, "Physics", Collider>
	{
		bool IsTrigger() const override { return isTrigger; };
		ColliderShape Shape() const override { return ColliderShape::Sphere; };

		// Where it is and how big, in world space, posed by world.
		void InWorld(const glm::mat4& world, glm::vec3& worldCenter, float& worldRadius) const;

		Serial<glm::vec3> center;
		Serial<float> radius = 0.5f;
		Serial<bool> isTrigger;

	protected:
		bool Refresh() const override;

		Bounds BoundsIn(const glm::mat4& world) const override;

		// A circle around each world axis, as Unity draws it.
		void WireframeIn(const glm::mat4& world, std::vector<glm::vec3>& lines) const override;
		bool Intersect(const glm::mat4& world, const Ray& ray, float maxDistance, RaycastHit& hit) const override;

	private:
		// What center and radius were when last refreshed.
		mutable glm::vec3 m_center{ 0.0f };
		mutable float m_radius = 0.0f;
	};
};
