#include "Collider.h"

#include "glm/gtc/constants.hpp"

#include <algorithm>
#include <cmath>


namespace Loom
{
	namespace
	{
		constexpr int BOX_CORNERS = 8;
		constexpr int CIRCLE_SEGMENTS = 32;

		glm::mat4 WorldOf(const GameObject* gameObject)
		{
			return gameObject ? gameObject->WorldMatrix() : glm::mat4(1.0f);
		};
	};

	Collider::~Collider()
	{
		if (m_tree)
			m_tree->Remove(*this);
	};

	void Collider::OnPhysics()
	{
		if (ColliderTree::current)
			ColliderTree::current->Sync(*this);
	};

	void Collider::CatchUp() const
	{
		if (Refresh() && m_tree)
			m_tree->Resize(*this);
	};

	bool Collider::Raycast(const Ray& ray, RaycastHit& hit, float maxDistance)
	{
		CatchUp();

		return RaycastAt(WorldOf(m_gameObject), ray, hit, maxDistance);
	};

	Bounds Collider::WorldBounds() const
	{
		CatchUp();

		return BoundsIn(WorldOf(m_gameObject));
	};

	std::vector<glm::vec3> Collider::Wireframe() const
	{
		std::vector<glm::vec3> lines{ };

		CatchUp();
		WireframeIn(WorldOf(m_gameObject), lines);

		return lines;
	};

	bool Collider::RaycastAt(const glm::mat4& world, const Ray& ray, RaycastHit& hit, float maxDistance)
	{
		if (glm::length(ray.direction) == 0.0f)
			return false;

		RaycastHit found;

		if (!Intersect(world, { ray.origin, glm::normalize(ray.direction) }, maxDistance, found))
			return false;

		found.collider = this;
		found.gameObject = m_gameObject;
		hit = found;

		return true;
	};

	bool BoxCollider::Refresh() const
	{
		if (*center == m_center && *size == m_size)
			return false;

		m_center = *center;
		m_size = *size;

		return true;
	};

	Bounds BoxCollider::BoundsIn(const glm::mat4& world) const
	{
		const glm::vec3 half = glm::abs(*size) * 0.5f;

		const glm::vec3 middle = glm::vec3(world * glm::vec4(*center, 1.0f));

		// Each local axis's half-length, laid along the world axes it now spans.
		glm::vec3 reach(0.0f);

		for (int axis = 0; axis < glm::vec3::length(); axis++)
			reach += glm::abs(glm::vec3(world[axis])) * half[axis];

		return { middle - reach, middle + reach };
	};

	void BoxCollider::WireframeIn(const glm::mat4& world, std::vector<glm::vec3>& lines) const
	{
		const glm::vec3 half = glm::abs(*size) * 0.5f;

		// Corner i is on the + side of each axis whose bit it has set, so each
		// edge joins two corners a single bit apart.
		const auto corner =
			[&](int i)
			{
				glm::vec3 local = *center;

				for (int axis = 0; axis < glm::vec3::length(); axis++)
					local[axis] += (i >> axis & 1) ? half[axis] : -half[axis];

				return glm::vec3(world * glm::vec4(local, 1.0f));
			};

		for (int i = 0; i < BOX_CORNERS; i++)
			for (int axis = 0; axis < glm::vec3::length(); axis++)
				if ((i >> axis & 1) == 0)
				{
					lines.push_back(corner(i));
					lines.push_back(corner(i | 1 << axis));
				};
	};

	bool BoxCollider::Intersect(const glm::mat4& world, const Ray& ray, float maxDistance, RaycastHit& hit) const
	{
		// Flattened by a zero scale: nothing to hit, and no inverse to find.
		if (glm::determinant(glm::mat3(world)) == 0.0f)
			return false;

		const glm::mat4 toLocal = glm::inverse(world);

		// The same parameter reaches the same point in either space, so the
		// distance found locally is already the world distance.
		const Ray local
		{
			glm::vec3(toLocal * glm::vec4(ray.origin, 1.0f)),
			glm::mat3(toLocal) * ray.direction
		};

		const glm::vec3 half = glm::abs(*size) * 0.5f;
		const Bounds box{ *center - half, *center + half };

		float entry, exit;
		int axis;

		if (!box.IntersectRay(local, entry, exit, axis) || entry < 0.0f || entry > maxDistance)
			return false;

		glm::vec3 normal(0.0f);
		normal[axis] = local.direction[axis] > 0.0f ? -1.0f : 1.0f;

		hit.point = ray.GetPoint(entry);
		hit.normal = glm::normalize(glm::transpose(glm::mat3(toLocal)) * normal);
		hit.distance = entry;

		return true;
	};

	bool SphereCollider::Refresh() const
	{
		if (*center == m_center && *radius == m_radius)
			return false;

		m_center = *center;
		m_radius = *radius;

		return true;
	};

	void SphereCollider::Place(const glm::mat4& world, glm::vec3& worldCenter, float& worldRadius) const
	{
		const float scale = std::max({
			glm::length(glm::vec3(world[0])),
			glm::length(glm::vec3(world[1])),
			glm::length(glm::vec3(world[2])) });

		worldCenter = glm::vec3(world * glm::vec4(*center, 1.0f));
		worldRadius = std::abs(*radius) * scale;
	};

	Bounds SphereCollider::BoundsIn(const glm::mat4& world) const
	{
		glm::vec3 middle;
		float reach;

		Place(world, middle, reach);

		return { middle - glm::vec3(reach), middle + glm::vec3(reach) };
	};

	void SphereCollider::WireframeIn(const glm::mat4& world, std::vector<glm::vec3>& lines) const
	{
		glm::vec3 middle;
		float reach;

		Place(world, middle, reach);

		for (int axis = 0; axis < glm::vec3::length(); axis++)
		{
			const int across = (axis + 1) % glm::vec3::length();
			const int up = (axis + 2) % glm::vec3::length();

			const auto around =
				[&](int segment)
				{
					const float turn = glm::two_pi<float>() * segment / CIRCLE_SEGMENTS;

					glm::vec3 point = middle;
					point[across] += std::cos(turn) * reach;
					point[up] += std::sin(turn) * reach;

					return point;
				};

			for (int segment = 0; segment < CIRCLE_SEGMENTS; segment++)
			{
				lines.push_back(around(segment));
				lines.push_back(around(segment + 1));
			};
		};
	};

	bool SphereCollider::Intersect(const glm::mat4& world, const Ray& ray, float maxDistance, RaycastHit& hit) const
	{
		glm::vec3 middle;
		float reach;

		Place(world, middle, reach);

		if (reach <= 0.0f)
			return false;

		const glm::vec3 offset = ray.origin - middle;
		const float along = glm::dot(offset, ray.direction);
		const float outside = glm::dot(offset, offset) - reach * reach;

		// Starting inside, or on the surface, is not a hit.
		if (outside <= 0.0f)
			return false;

		const float discriminant = along * along - outside;

		if (discriminant < 0.0f)
			return false;

		const float entry = -along - std::sqrt(discriminant);

		if (entry < 0.0f || entry > maxDistance)
			return false;

		hit.point = ray.GetPoint(entry);
		hit.normal = (hit.point - middle) / reach;
		hit.distance = entry;

		return true;
	};
};
