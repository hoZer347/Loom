#pragma once

#include "Loom API.h"

#include "Physics.h"

#include <functional>
#include <vector>


namespace Loom
{
	struct Collider;

	/**
	* Loom::ColliderTree
	* - A scene's colliders, in a dynamic bounding volume hierarchy: every
	*   collider is a leaf holding its world bounds padded by a margin, and
	*   every branch holds the union of its two children, so a ray only visits
	*   the branches it passes through
	* - Leaves go in where they add the least surface area, and the tree
	*   rotates itself back into balance on the way up, as Box2D's does
	* - A collider moving inside its padded bounds costs nothing; only one that
	*   leaves them is taken out and put back
	* - Scene::Physics keeps it in step with the hierarchy: BeginSync, then
	*   Sync from every enabled collider's OnPhysics, then EndSync drops every
	*   collider that was not synced, being disabled, detached or moved to
	*   another scene
	*/
	struct LOOM_API ColliderTree final
	{
		static constexpr int NONE = -1;

		ColliderTree() = default;
		ColliderTree(const ColliderTree&) = delete;
		ColliderTree& operator=(const ColliderTree&) = delete;

		// Lets go of every collider still in it, so none removes itself from a
		// tree that is gone.
		~ColliderTree();

		void BeginSync();
		void Sync(Collider& collider);
		void EndSync();

		void Remove(Collider& collider);

		// Refits collider's leaf to what it covers where it was last synced,
		// for a shape that changed between syncs.
		void Resize(const Collider& collider);

		// Calls visit with every hit within maxDistance on a collider where it
		// was last synced, roughly nearest first. visit answers the distance to
		// keep looking within: the hit's, to find only nearer ones, or the one
		// it was given, to see them all.
		typedef std::function<float(const RaycastHit& hit, float maxDistance)> Visitor;
		void Raycast(const Ray& ray, float maxDistance, const Visitor& visit) const;

		// Calls visit for every collider whose padded bounds overlap bounds.
		void Query(const Bounds& bounds, const std::function<void(Collider&)>& visit) const;

		// Told of every collider that leaves the tree, while it still exists.
		std::function<void(const Collider&)> onRemove{ };

		size_t Size() const { return m_leaves; };

		// Leaves are height 0. An empty tree is height -1.
		int Height() const;

		const Bounds* PaddedBounds(const Collider& collider) const;

		// The current tree during Scene::Physics, which a collider's OnPhysics
		// syncs itself into.
		static inline ColliderTree* current = nullptr;

	private:
		struct Node
		{
			Bounds bounds;
			Collider* collider = nullptr;

			// Doubles as the next free node while this one is free.
			int parent = NONE;
			int children[2] = { NONE, NONE };

			// -1 while free.
			int height = 0;

			unsigned sync = 0;

			bool IsLeaf() const { return children[0] == NONE; };
		};

		int Allocate();
		void Free(int node);

		// Puts leaf where bounds needs it, unless its padding still covers them.
		void Fit(int leaf, const Bounds& bounds);

		void InsertLeaf(int leaf);
		void RemoveLeaf(int leaf);

		// Returns the node that ends up where node was.
		int Balance(int node);

		// Recomputes the bounds and height of every branch from node to the root.
		void Refit(int node);

		std::vector<Node> m_nodes{ };
		int m_root = NONE;
		int m_free = NONE;
		size_t m_leaves = 0;
		unsigned m_sync = 0;
	};
};
