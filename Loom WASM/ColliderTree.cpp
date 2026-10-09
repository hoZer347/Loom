#include "ColliderTree.h"

#include "Collider.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <vector>


namespace Loom
{
	namespace
	{
		// How far a leaf's bounds reach past its collider's, so a collider
		// moving a little each frame stays in its leaf.
		constexpr float PADDING = 0.1f;

		// Past this much slack a leaf is refitted anyway, so one that shrinks
		// or stops does not go on catching rays it is nowhere near.
		constexpr float MOST_PADDING = PADDING * 4.0f;

		// A branch whose children's heights differ by more than this is rotated.
		constexpr int MOST_IMBALANCE = 1;

		// Box2D's insertion cost: a new branch costs twice the area it covers,
		// once for itself and once for the leaf beside it.
		constexpr float BRANCH_COST = 2.0f;

		// Deep enough for any tree the rotations leave balanced, so a raycast
		// never touches the heap; a deeper one spills over rather than fails.
		constexpr size_t INLINE_PENDING = 64;

		// A stack held on the caller's own stack until it outgrows it.
		template <typename T, size_t INLINE>
		struct SmallStack final
		{
			void Push(const T& item)
			{
				if (m_count < INLINE)
					m_inline[m_count] = item;
				else
					m_spill.push_back(item);

				m_count++;
			};

			T Pop()
			{
				m_count--;

				if (m_count < INLINE)
					return m_inline[m_count];

				const T item = m_spill.back();
				m_spill.pop_back();

				return item;
			};

			bool Empty() const { return m_count == 0; };

		private:
			std::array<T, INLINE> m_inline;
			std::vector<T> m_spill{ };
			size_t m_count = 0;
		};

		// A node waiting to be visited, with the distance the ray enters it at,
		// which a nearer hit found meanwhile can rule it out by.
		struct Pending
		{
			int node;
			float entry;
		};
	};

	ColliderTree::~ColliderTree()
	{
		for (Node& node : m_nodes)
			if (node.collider)
				node.collider->m_tree = nullptr;
	};

	void ColliderTree::BeginSync()
	{
		m_sync++;
	};

	void ColliderTree::Sync(Collider& collider)
	{
		const glm::mat4 world = collider.m_gameObject->WorldMatrix();
		const bool reshaped = collider.Refresh();

		// Most colliders have not moved since the last step, and those leaves
		// already fit.
		if (collider.m_tree == this && !reshaped && world == collider.m_synced)
		{
			m_nodes[collider.m_node].sync = m_sync;
			return;
		};

		collider.m_synced = world;

		const Bounds bounds = collider.BoundsIn(collider.m_synced);

		if (collider.m_tree != this)
		{
			if (collider.m_tree)
				collider.m_tree->Remove(collider);

			const int leaf = Allocate();

			m_nodes[leaf].bounds = bounds.Expanded(PADDING);
			m_nodes[leaf].collider = &collider;
			m_nodes[leaf].height = 0;

			InsertLeaf(leaf);

			collider.m_tree = this;
			collider.m_node = leaf;
			m_leaves++;
		}
		else
			Fit(collider.m_node, bounds);

		m_nodes[collider.m_node].sync = m_sync;
	};

	void ColliderTree::Resize(const Collider& collider)
	{
		if (collider.m_tree == this)
			Fit(collider.m_node, collider.BoundsIn(collider.m_synced));
	};

	void ColliderTree::Fit(int leaf, const Bounds& bounds)
	{
		const Bounds& padded = m_nodes[leaf].bounds;

		if (padded.Contains(bounds) && bounds.Expanded(MOST_PADDING).Contains(padded))
			return;

		RemoveLeaf(leaf);
		m_nodes[leaf].bounds = bounds.Expanded(PADDING);
		InsertLeaf(leaf);
	};

	void ColliderTree::EndSync()
	{
		// By index: removing frees nodes but never moves them.
		for (size_t i = 0; i < m_nodes.size(); i++)
			if (m_nodes[i].collider && m_nodes[i].sync != m_sync)
				Remove(*m_nodes[i].collider);
	};

	void ColliderTree::Remove(Collider& collider)
	{
		if (collider.m_tree != this)
			return;

		if (onRemove)
			onRemove(collider);

		RemoveLeaf(collider.m_node);
		Free(collider.m_node);

		collider.m_tree = nullptr;
		collider.m_node = NONE;
		m_leaves--;
	};

	void ColliderTree::Raycast(const Ray& ray, float maxDistance, const Visitor& visit) const
	{
		SmallStack<Pending, INLINE_PENDING> pending;

		// Where the ray enters node within maxDistance, if it does.
		const auto reach =
			[&](int node) -> std::optional<Pending>
			{
				float entry, exit;
				int axis;

				if (m_nodes[node].bounds.IntersectRay(ray, entry, exit, axis)
					&& exit >= 0.0f
					&& entry <= maxDistance)
					return Pending{ node, entry };

				return std::nullopt;
			};

		if (m_root == NONE)
			return;

		if (const std::optional<Pending> root = reach(m_root))
			pending.Push(*root);

		while (!pending.Empty())
		{
			const Pending next = pending.Pop();

			if (next.entry > maxDistance)
				continue;

			const Node& current = m_nodes[next.node];

			if (current.IsLeaf())
			{
				RaycastHit hit;

				if (current.collider->RaycastAt(current.collider->m_synced, ray, hit, maxDistance))
					maxDistance = visit(hit, maxDistance);

				continue;
			};

			std::optional<Pending> first = reach(current.children[0]);
			std::optional<Pending> second = reach(current.children[1]);

			// The nearer child last, so it is the next one taken.
			if (first && second && first->entry < second->entry)
				std::swap(first, second);

			for (const std::optional<Pending>& child : { first, second })
				if (child)
					pending.Push(*child);
		};
	};

	void ColliderTree::Query(const Bounds& bounds, const std::function<void(Collider&)>& visit) const
	{
		if (m_root == NONE)
			return;

		SmallStack<int, INLINE_PENDING> pending;
		pending.Push(m_root);

		while (!pending.Empty())
		{
			const Node& node = m_nodes[pending.Pop()];

			if (!node.bounds.Overlaps(bounds))
				continue;

			if (node.IsLeaf())
			{
				visit(*node.collider);
				continue;
			};

			pending.Push(node.children[0]);
			pending.Push(node.children[1]);
		};
	};

	int ColliderTree::Height() const
	{
		return m_root == NONE ? -1 : m_nodes[m_root].height;
	};

	const Bounds* ColliderTree::PaddedBounds(const Collider& collider) const
	{
		return collider.m_tree == this ? &m_nodes[collider.m_node].bounds : nullptr;
	};

	int ColliderTree::Allocate()
	{
		if (m_free == NONE)
		{
			m_nodes.emplace_back();
			return (int)m_nodes.size() - 1;
		};

		const int node = m_free;
		m_free = m_nodes[node].parent;
		m_nodes[node] = Node{ };

		return node;
	};

	void ColliderTree::Free(int node)
	{
		m_nodes[node] = Node{ };
		m_nodes[node].parent = m_free;
		m_nodes[node].height = -1;
		m_free = node;
	};

	void ColliderTree::InsertLeaf(int leaf)
	{
		if (m_root == NONE)
		{
			m_root = leaf;
			m_nodes[leaf].parent = NONE;
			return;
		};

		const Bounds bounds = m_nodes[leaf].bounds;

		// Down to the sibling that costs the least surface area to pair with,
		// counting what every branch above it grows by.
		int sibling = m_root;

		while (!m_nodes[sibling].IsLeaf())
		{
			const Node& branch = m_nodes[sibling];
			const float combined = branch.bounds.Union(bounds).HalfArea();

			const float here = BRANCH_COST * combined;
			const float inherited = BRANCH_COST * (combined - branch.bounds.HalfArea());

			float below[2];

			for (int side = 0; side < 2; side++)
			{
				const Node& child = m_nodes[branch.children[side]];
				const float grown = child.bounds.Union(bounds).HalfArea();

				below[side] = inherited + (child.IsLeaf() ? grown : grown - child.bounds.HalfArea());
			};

			if (here < below[0] && here < below[1])
				break;

			sibling = branch.children[below[0] < below[1] ? 0 : 1];
		};

		const int branch = Allocate();
		const int parent = m_nodes[sibling].parent;

		m_nodes[branch].parent = parent;
		m_nodes[branch].children[0] = sibling;
		m_nodes[branch].children[1] = leaf;

		if (parent == NONE)
			m_root = branch;
		else
		{
			int* children = m_nodes[parent].children;
			children[children[0] == sibling ? 0 : 1] = branch;
		};

		m_nodes[sibling].parent = branch;
		m_nodes[leaf].parent = branch;

		Refit(branch);
	};

	void ColliderTree::RemoveLeaf(int leaf)
	{
		if (leaf == m_root)
		{
			m_root = NONE;
			return;
		};

		const int parent = m_nodes[leaf].parent;
		const int grandparent = m_nodes[parent].parent;
		const int* siblings = m_nodes[parent].children;
		const int sibling = siblings[siblings[0] == leaf ? 1 : 0];

		m_nodes[sibling].parent = grandparent;
		Free(parent);

		if (grandparent == NONE)
		{
			m_root = sibling;
			return;
		};

		int* children = m_nodes[grandparent].children;
		children[children[0] == parent ? 0 : 1] = sibling;

		Refit(grandparent);
	};

	int ColliderTree::Balance(int a)
	{
		if (m_nodes[a].IsLeaf())
			return a;

		const int imbalance =
			m_nodes[m_nodes[a].children[1]].height - m_nodes[m_nodes[a].children[0]].height;

		if (std::abs(imbalance) <= MOST_IMBALANCE)
			return a;

		// The taller child c takes a's place and a goes under it, keeping the
		// shorter of c's children; c keeps the taller.
		const int side = imbalance > 0 ? 1 : 0;
		const int b = m_nodes[a].children[1 - side];
		const int c = m_nodes[a].children[side];

		const int* grandchildren = m_nodes[c].children;
		const bool firstTaller = m_nodes[grandchildren[0]].height > m_nodes[grandchildren[1]].height;
		const int taller = grandchildren[firstTaller ? 0 : 1];
		const int shorter = grandchildren[firstTaller ? 1 : 0];

		const int parent = m_nodes[a].parent;

		m_nodes[c].parent = parent;
		m_nodes[c].children[0] = a;
		m_nodes[c].children[1] = taller;

		if (parent == NONE)
			m_root = c;
		else
		{
			int* children = m_nodes[parent].children;
			children[children[0] == a ? 0 : 1] = c;
		};

		m_nodes[a].parent = c;
		m_nodes[a].children[side] = shorter;
		m_nodes[shorter].parent = a;

		m_nodes[a].bounds = m_nodes[b].bounds.Union(m_nodes[shorter].bounds);
		m_nodes[a].height = 1 + std::max(m_nodes[b].height, m_nodes[shorter].height);

		return c;
	};

	void ColliderTree::Refit(int node)
	{
		while (node != NONE)
		{
			node = Balance(node);

			Node& branch = m_nodes[node];
			const Node& first = m_nodes[branch.children[0]];
			const Node& second = m_nodes[branch.children[1]];

			branch.bounds = first.bounds.Union(second.bounds);
			branch.height = 1 + std::max(first.height, second.height);

			node = branch.parent;
		};
	};
};
