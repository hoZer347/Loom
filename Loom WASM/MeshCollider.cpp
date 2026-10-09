#include "MeshCollider.h"

#include "Mesh.h"

#include <algorithm>
#include <array>
#include <cmath>


namespace Loom
{
	namespace
	{
		constexpr size_t COMPONENTS = 3;
		constexpr uint32_t CORNERS = 3;

		// At most this many triangles are tested one by one rather than split.
		constexpr uint32_t LEAF_TRIANGLES = 4;

		// Splitting at the median halves every branch, so the tree is never
		// deeper than log2 of its triangles: far below this for any mesh that
		// fits in memory.
		constexpr size_t MOST_DEPTH = 64;

		Bounds Around(const glm::vec3& point)
		{
			return { point, point };
		};
	};

	glm::vec3 MeshCollider::Triangle::Centre() const
	{
		return (corners[0] + corners[1] + corners[2]) / (float)CORNERS;
	};

	bool MeshCollider::Refresh() const
	{
		static const std::vector<float> no_vertices{ };
		static const std::vector<uint32_t> no_indices{ };

		Mesh* mesh = m_gameObject ? m_gameObject->GetComponent<Mesh>() : nullptr;

		const std::vector<float>& vertices = mesh ? *mesh->m_vertices : no_vertices;
		const std::vector<uint32_t>& indices = mesh ? mesh->m_indices : no_indices;
		const uint32_t primitive = mesh ? (uint32_t)*mesh->primitive_id : (uint32_t)Mesh::Triangles;

		if (primitive == m_primitive && vertices == m_source && indices == m_indices)
			return false;

		m_source = vertices;
		m_indices = indices;
		m_primitive = primitive;

		Rebuild();

		return true;
	};

	void MeshCollider::Rebuild() const
	{
		m_triangles.clear();
		m_nodes.clear();

		const size_t vertices = m_source.size() / COMPONENTS;

		// The corners in drawing order: through the indices when there are any.
		const size_t points = m_indices.empty() ? vertices : m_indices.size();

		const auto vertex =
			[&](size_t corner)
			{
				return m_indices.empty() ? corner : (size_t)m_indices[corner];
			};

		const auto point =
			[&](size_t i)
			{
				return glm::vec3(m_source[i * COMPONENTS], m_source[i * COMPONENTS + 1], m_source[i * COMPONENTS + 2]);
			};

		// An index past the vertices leaves its triangle out.
		const auto add =
			[&](size_t a, size_t b, size_t c)
			{
				const size_t corners[] = { vertex(a), vertex(b), vertex(c) };

				for (const size_t corner : corners)
					if (corner >= vertices)
						return;

				m_triangles.push_back({ { point(corners[0]), point(corners[1]), point(corners[2]) } });
			};

		switch (m_primitive)
		{
		case Mesh::Triangles:
			for (size_t i = 0; i + CORNERS <= points; i += CORNERS)
				add(i, i + 1, i + 2);
			break;

		case Mesh::TriangleStrip:
			for (size_t i = 0; i + CORNERS <= points; i++)
				add(i, i + 1, i + 2);
			break;

		case Mesh::TriangleFan:
			for (size_t i = 1; i + 1 < points; i++)
				add(0, i, i + 1);
			break;

		default:
			break;
		};

		if (!m_triangles.empty())
			Build(0, (uint32_t)m_triangles.size());
	};

	void MeshCollider::Build(uint32_t first, uint32_t count) const
	{
		const uint32_t node = (uint32_t)m_nodes.size();
		m_nodes.emplace_back();

		Bounds bounds = Around(m_triangles[first].corners[0]);
		Bounds centres = Around(m_triangles[first].Centre());

		for (uint32_t i = first; i < first + count; i++)
		{
			for (const glm::vec3& corner : m_triangles[i].corners)
				bounds = bounds.Union(Around(corner));

			centres = centres.Union(Around(m_triangles[i].Centre()));
		};

		m_nodes[node].bounds = bounds;

		if (count <= LEAF_TRIANGLES)
		{
			m_nodes[node].first = first;
			m_nodes[node].count = count;
			return;
		};

		// Halved along whichever way the triangles' centres spread the most.
		const glm::vec3 spread = centres.max - centres.min;
		const int axis = spread.x > spread.y
			? (spread.x > spread.z ? 0 : 2)
			: (spread.y > spread.z ? 1 : 2);

		const uint32_t half = count / 2;

		std::nth_element(
			m_triangles.begin() + first,
			m_triangles.begin() + first + half,
			m_triangles.begin() + first + count,
			[axis](const Triangle& a, const Triangle& b) { return a.Centre()[axis] < b.Centre()[axis]; });

		Build(first, half);
		m_nodes[node].second = (uint32_t)m_nodes.size();
		Build(first + half, count - half);
	};

	Bounds MeshCollider::BoundsIn(const glm::mat4& world) const
	{
		if (m_nodes.empty())
			return Around(glm::vec3(world[3]));

		const Bounds& local = m_nodes[0].bounds;
		const glm::vec3 middle = glm::vec3(world * glm::vec4(local.Center(), 1.0f));
		const glm::vec3 half = local.Extents();

		glm::vec3 reach(0.0f);

		for (int axis = 0; axis < glm::vec3::length(); axis++)
			reach += glm::abs(glm::vec3(world[axis])) * half[axis];

		return { middle - reach, middle + reach };
	};

	void MeshCollider::WireframeIn(const glm::mat4& world, std::vector<glm::vec3>& lines) const
	{
		for (const Triangle& triangle : m_triangles)
			for (uint32_t corner = 0; corner < CORNERS; corner++)
			{
				lines.push_back(glm::vec3(world * glm::vec4(triangle.corners[corner], 1.0f)));
				lines.push_back(glm::vec3(world * glm::vec4(triangle.corners[(corner + 1) % CORNERS], 1.0f)));
			};
	};

	bool MeshCollider::Intersect(const glm::mat4& world, const Ray& ray, float maxDistance, RaycastHit& hit) const
	{
		if (m_nodes.empty() || glm::determinant(glm::mat3(world)) == 0.0f)
			return false;

		const glm::mat4 toLocal = glm::inverse(world);

		// As in BoxCollider: the same parameter is the world distance.
		const Ray local
		{
			glm::vec3(toLocal * glm::vec4(ray.origin, 1.0f)),
			glm::mat3(toLocal) * ray.direction
		};

		float nearest = maxDistance;
		glm::vec3 normal(0.0f);
		bool found = false;

		std::array<uint32_t, MOST_DEPTH> pending;
		size_t count = 0;
		pending[count++] = 0;

		while (count > 0)
		{
			const Node& node = m_nodes[pending[--count]];

			float entry, exit;
			int axis;

			if (!node.bounds.IntersectRay(local, entry, exit, axis) || exit < 0.0f || entry > nearest)
				continue;

			if (node.count == 0)
			{
				pending[count++] = node.second;
				pending[count++] = (uint32_t)(&node - m_nodes.data()) + 1;
				continue;
			};

			// Moller and Trumbore's test, in their names, from either side.
			for (uint32_t i = node.first; i < node.first + node.count; i++)
			{
				const glm::vec3* corners = m_triangles[i].corners;
				const glm::vec3 edge1 = corners[1] - corners[0];
				const glm::vec3 edge2 = corners[2] - corners[0];

				const glm::vec3 p = glm::cross(local.direction, edge2);
				const float determinant = glm::dot(edge1, p);

				if (determinant == 0.0f)
					continue;

				const glm::vec3 offset = local.origin - corners[0];
				const float u = glm::dot(offset, p) / determinant;

				if (u < 0.0f || u > 1.0f)
					continue;

				const glm::vec3 q = glm::cross(offset, edge1);
				const float v = glm::dot(local.direction, q) / determinant;

				if (v < 0.0f || u + v > 1.0f)
					continue;

				const float distance = glm::dot(edge2, q) / determinant;

				if (distance < 0.0f || distance > nearest)
					continue;

				nearest = distance;
				normal = glm::cross(edge1, edge2);
				found = true;
			};
		};

		if (!found)
			return false;

		glm::vec3 outward = glm::normalize(glm::transpose(glm::mat3(toLocal)) * normal);

		if (glm::dot(outward, ray.direction) > 0.0f)
			outward = -outward;

		hit.point = ray.GetPoint(nearest);
		hit.normal = outward;
		hit.distance = nearest;

		return true;
	};
};
