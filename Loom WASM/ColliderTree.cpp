#include "ColliderTree.h"

#include "Collider.h"
#include "Jobs.h"
#include "MeshCollider.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#if defined(_M_X64) || defined(__SSE2__)
#include <immintrin.h>
#endif


namespace Loom
{
	namespace
	{
		// How far a leaf's bounds reach past its collider's, so a collider
		// moving a little each frame stays in its leaf.
		constexpr float PADDING = 0.05f;

		// Past this much slack a leaf is refitted anyway, so one that shrinks
		// or stops does not go on catching rays it is nowhere near.
		constexpr float MOST_PADDING = PADDING * 4.0f;

		// How many steps' travel a moving leaf is stretched ahead by when it
		// is refitted. It is refitted once it no longer covers the next one.
		// Measured against three and six on the Crowd benchmark, as the
		// padding was against a tenth.
		constexpr float PREDICTION = 10.0f;

		// The inverse of a ray's direction along an axis it runs parallel to:
		// large enough that it crosses that axis's slab nowhere near.
		constexpr float PARALLEL = 1e30f;

		// The shuffles that bring a vector's y and z to its first lane, and
		// the mask of the first three lanes' signs.
		constexpr int Y_LANE = 1;
		constexpr int Z_LANE = 2;
		constexpr int AXIS_LANES = 0b0111;

		// What a ray's entry into a node it misses is taken as.
		constexpr float MISSED = std::numeric_limits<float>::infinity();

		// bounds stretched along ahead, to cover where it is going.
		Bounds Stretched(Bounds bounds, const glm::vec3& ahead)
		{
			bounds.min += glm::min(ahead, glm::vec3(0.0f));
			bounds.max += glm::max(ahead, glm::vec3(0.0f));

			return bounds;
		};

		Bounds Padded(const Bounds& bounds, const glm::vec3& travel)
		{
			return Stretched(bounds, travel * PREDICTION).Expanded(PADDING);
		};

		// How many colliders one job's chunk measures.
		constexpr size_t MEASURE_GRAIN = 64;

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

	bool ColliderTree::Unmoved(const Collider& collider) const
	{
		if (collider.m_tree != this)
			return false;

		int level = 0;

		for (const GameObject* at = collider.m_gameObject; at != nullptr; at = at->GetParent(), level++)
		{
			if (level == collider.m_placedLevels)
				return false;

			const Collider::Placed& placed = collider.m_placed[level];

			if (placed.object != at || *at->transform.position != placed.position
				|| *at->transform.rotation != placed.rotation || *at->transform.scale != placed.scale)
				return false;
		};

		return level == collider.m_placedLevels;
	};

	bool ColliderTree::Measure(Collider& collider) const
	{
		// One whose transforms are all as they were has its world as it was.
		return Measure(collider, Unmoved(collider) ? collider.m_synced : collider.m_gameObject->WorldMatrix());
	};

	bool ColliderTree::Measure(Collider& collider, const glm::mat4& world) const
	{
		const bool reshaped = collider.Refresh();

		// Most colliders have not moved since the last step, and those leaves
		// already fit.
		if (collider.m_tree == this && !reshaped && world == collider.m_synced)
			return false;

		collider.m_synced = world;

		int level = 0;
		const GameObject* at = collider.m_gameObject;

		for (; at != nullptr && level < Collider::SYNCED_LEVELS; at = at->GetParent(), level++)
			collider.m_placed[level] = { at, at->transform.position, at->transform.rotation, at->transform.scale };

		// Too far down to keep them all, none are kept.
		collider.m_placedLevels = at == nullptr ? level : 0;

		collider.m_bounds = collider.BoundsIn(collider.m_synced);
		collider.m_shape = Contacts::Pose(collider, collider.m_synced);

		return true;
	};

	void ColliderTree::SyncEnlisted()
	{
		m_measured.resize(m_enlisted.size());

		Jobs::For(m_enlisted.size(), MEASURE_GRAIN,
			[&](size_t, size_t begin, size_t end)
			{
				for (size_t i = begin; i < end; i++)
				{
					Collider& collider = *m_enlisted[i];
					m_measured[i] = Measure(collider) || collider.m_tree != this;

					// Each leaf is its own, so stamped at once.
					if (!m_measured[i])
						m_nodes[collider.m_node].sync = m_sync;
				};
			});

		for (size_t i = 0; i < m_enlisted.size(); i++)
			if (m_measured[i])
				Place(*m_enlisted[i], true);

		m_enlisted.clear();
	};

	void ColliderTree::Place(Collider& collider, bool moved, const glm::vec3& travel)
	{
		if (!moved && collider.m_tree == this)
		{
			m_nodes[collider.m_node].sync = m_sync;
			return;
		};

		const Bounds& bounds = collider.m_bounds;

		if (collider.m_tree != this)
		{
			if (collider.m_tree)
			{
				if (collider.m_tree->onChange)
					collider.m_tree->onChange();

				collider.m_tree->Remove(collider);
			};

			const int leaf = Allocate();

			m_nodes[leaf].bounds = Padded(bounds, travel);
			m_nodes[leaf].collider = &collider;
			m_nodes[leaf].height = 0;

			InsertLeaf(leaf);
			m_moved.push_back(&collider);

			collider.m_tree = this;
			collider.m_node = leaf;
			m_leaves++;
		}
		else
			Fit(collider.m_node, bounds, travel);

		m_nodes[collider.m_node].sync = m_sync;
	};

	void ColliderTree::Resize(const Collider& collider)
	{
		if (collider.m_tree != this)
			return;

		if (onChange)
			onChange();

		collider.m_bounds = collider.BoundsIn(collider.m_synced);
		collider.m_shape = Contacts::Pose(collider, collider.m_synced);
		Fit(collider.m_node, collider.m_bounds, glm::vec3(0.0f));
	};

	void ColliderTree::Reach(const Collider& collider, const glm::vec3& travel)
	{
		if (collider.m_tree == this)
			Fit(collider.m_node, collider.m_bounds, travel);
	};

	bool ColliderTree::Fits(const Collider& collider, const glm::vec3& travel) const
	{
		if (collider.m_tree != this)
			return false;

		const Bounds& padded = m_nodes[collider.m_node].bounds;

		// As much slack behind as ahead, so a leaf a steadily moving
		// collider has moved along is not taken for one too big.
		const Bounds most = Stretched(Padded(collider.m_bounds, travel), -travel * PREDICTION).Expanded(MOST_PADDING);

		return padded.Contains(Stretched(collider.m_bounds, travel)) && most.Contains(padded);
	};

	void ColliderTree::Fit(int leaf, const Bounds& bounds, const glm::vec3& travel)
	{
		if (Fits(*m_nodes[leaf].collider, travel))
			return;

		RemoveLeaf(leaf);
		m_nodes[leaf].bounds = Padded(bounds, travel);
		InsertLeaf(leaf);

		m_moved.push_back(m_nodes[leaf].collider);
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

		std::erase(m_moved, &collider);

		RemoveLeaf(collider.m_node);
		Free(collider.m_node);

		collider.m_tree = nullptr;
		collider.m_node = NONE;
		m_leaves--;
	};

	void ColliderTree::Raycast(const Ray& ray, float maxDistance, const Visitor& visit) const
	{
		SmallStack<Pending, INLINE_PENDING> pending;

		// Each axis's slab is crossed at (plane - origin) / direction; the
		// division is done once here. Parallel to an axis, the ray crosses
		// that slab nowhere, which a huge inverse with the same sign gives.
		glm::vec3 inverse;

		for (int axis = 0; axis < glm::vec3::length(); axis++)
		{
			const float along = ray.direction[axis];
			inverse[axis] = along != 0.0f ? 1.0f / along : std::copysign(PARALLEL, along);
		};

		const glm::vec3 origin = ray.origin;

#if defined(_M_X64) || defined(__SSE2__)
		// All three axes at once. Each load's fourth lane is whatever follows
		// in the node, and only the first three are ever read back.
		const __m128 from = _mm_setr_ps(origin.x, origin.y, origin.z, 0.0f);
		const __m128 across = _mm_setr_ps(inverse.x, inverse.y, inverse.z, 0.0f);

		const auto entering =
			[&](int node)
			{
				const Bounds& bounds = m_nodes[node].bounds;
				const __m128 toMin = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(&bounds.min.x), from), across);
				const __m128 toMax = _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(&bounds.max.x), from), across);

				const __m128 near = _mm_min_ps(toMin, toMax);
				const __m128 far = _mm_max_ps(toMin, toMax);

				const float entry = _mm_cvtss_f32(_mm_max_ss(_mm_max_ss(near, _mm_shuffle_ps(near, near, Y_LANE)), _mm_shuffle_ps(near, near, Z_LANE)));
				const float exit = _mm_cvtss_f32(_mm_min_ss(_mm_min_ss(far, _mm_shuffle_ps(far, far, Y_LANE)), _mm_shuffle_ps(far, far, Z_LANE)));

				return entry <= exit && exit >= 0.0f ? entry : MISSED;
			};
#else
		const auto entering =
			[&](int node)
			{
				const Bounds& bounds = m_nodes[node].bounds;
				const glm::vec3 toMin = (bounds.min - origin) * inverse;
				const glm::vec3 toMax = (bounds.max - origin) * inverse;

				const glm::vec3 near = glm::min(toMin, toMax);
				const glm::vec3 far = glm::max(toMin, toMax);

				const float entry = std::max(std::max(near.x, near.y), near.z);
				const float exit = std::min(std::min(far.x, far.y), far.z);

				return entry <= exit && exit >= 0.0f ? entry : MISSED;
			};
#endif

		const auto within =
			[&](float entry) { return entry != MISSED && entry <= maxDistance; };

		if (m_root == NONE || !within(entering(m_root)))
			return;

		pending.Push({ m_root, 0.0f });

		while (!pending.Empty())
		{
			const Pending next = pending.Pop();

			if (next.entry > maxDistance)
				continue;

			const Node& current = m_nodes[next.node];

			if (current.IsLeaf())
			{
				Collider& collider = *current.collider;
				RaycastHit hit;

				// Each is tested as it was posed at its last sync.
				const bool struck = collider.m_shape.kind == Contacts::Shape::MESH
					? collider.m_shape.mesh->IntersectPosed(ray, maxDistance, hit)
					: Contacts::Raycast(collider.m_shape, ray.origin, ray.direction, maxDistance, hit.distance, hit.normal);

				if (struck)
				{
					hit.point = ray.GetPoint(hit.distance);
					hit.collider = &collider;
					hit.gameObject = collider.m_gameObject;

					maxDistance = visit(hit, maxDistance);
				};

				continue;
			};

			const int first = current.children[0];
			const int second = current.children[1];
			const float firstEntry = entering(first);
			const float secondEntry = entering(second);

			// The nearer child last, so it is the next one taken.
			const bool firstNearer = firstEntry < secondEntry;
			const Pending nearer = firstNearer ? Pending{ first, firstEntry } : Pending{ second, secondEntry };
			const Pending farther = firstNearer ? Pending{ second, secondEntry } : Pending{ first, firstEntry };

			if (within(farther.entry))
				pending.Push(farther);

			if (within(nearer.entry))
				pending.Push(nearer);
		};
	};

	void ColliderTree::Query(const Bounds& bounds, const std::function<void(Collider&)>& visit) const
	{
		if (m_root == NONE)
			return;

		SmallStack<int, INLINE_PENDING> pending;
		pending.Push(m_root);

#if defined(_M_X64) || defined(__SSE2__)
		const __m128 lowest = _mm_setr_ps(bounds.min.x, bounds.min.y, bounds.min.z, 0.0f);
		const __m128 highest = _mm_setr_ps(bounds.max.x, bounds.max.y, bounds.max.z, 0.0f);
#endif

		while (!pending.Empty())
		{
			const Node& node = m_nodes[pending.Pop()];

#if defined(_M_X64) || defined(__SSE2__)
			// All three axes at once, as Raycast does: apart along any of
			// the first three lanes is apart.
			const __m128 above = _mm_cmpgt_ps(_mm_loadu_ps(&node.bounds.min.x), highest);
			const __m128 below = _mm_cmplt_ps(_mm_loadu_ps(&node.bounds.max.x), lowest);

			if (_mm_movemask_ps(_mm_or_ps(above, below)) & AXIS_LANES)
				continue;
#else
			if (!node.bounds.Overlaps(bounds))
				continue;
#endif

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
			const int balanced = Balance(node);

			Node& branch = m_nodes[balanced];
			const Node& first = m_nodes[branch.children[0]];
			const Node& second = m_nodes[branch.children[1]];

			const Bounds bounds = first.bounds.Union(second.bounds);
			const int height = 1 + std::max(first.height, second.height);

			// A branch left as it was leaves everything above it as it was.
			if (balanced == node && height == branch.height && bounds.min == branch.bounds.min && bounds.max == branch.bounds.max)
				return;

			branch.bounds = bounds;
			branch.height = height;

			node = branch.parent;
		};
	};
};
