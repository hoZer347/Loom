#include "Contacts.h"

#include "Collider.h"
#include "MeshCollider.h"

#include <algorithm>
#include <array>
#include <cmath>


namespace Loom
{
namespace Contacts
{
	namespace
	{
		constexpr int AXES = 3;
		constexpr int CORNERS = 3;
		constexpr int FACE_CORNERS = 4;

		// Below this a direction or a length is taken as none at all.
		constexpr float EPSILON = 1e-6f;

		// An edge pair only takes over from a face when it overlaps this much
		// less, so a box resting on a face keeps the same contacts from step
		// to step rather than flickering to an edge.
		constexpr float FACE_SLACK = 0.01f;

		constexpr float HALF = 0.5f;

		// A face clipped by four planes has at most eight corners.
		constexpr int MOST_CORNERS = 8;

		struct Sphere
		{
			glm::vec3 center;
			float radius;
		};

		// Its axes are unit length; half is how far it reaches along each.
		struct Box
		{
			glm::vec3 center;
			glm::vec3 axes[AXES];
			glm::vec3 half;
		};

		using Triangle = std::array<glm::vec3, CORNERS>;

		// Points held in place, so clipping never reaches for the heap.
		struct Polygon
		{
			glm::vec3 points[MOST_CORNERS];
			float depths[MOST_CORNERS];
			int count = 0;

			void Add(const glm::vec3& point, float depth = 0.0f)
			{
				if (count < MOST_CORNERS)
				{
					points[count] = point;
					depths[count] = depth;
					count++;
				};
			};
		};

		bool BoxOf(const BoxCollider& collider, const glm::mat4& world, Box& box)
		{
			box.center = glm::vec3(world * glm::vec4(*collider.center, 1.0f));

			for (int axis = 0; axis < AXES; axis++)
			{
				const glm::vec3 column(world[axis]);
				const float length = glm::length(column);

				if (length < EPSILON)
					return false;

				box.axes[axis] = column / length;
				box.half[axis] = std::abs((*collider.size)[axis]) * HALF * length;
			};

			return true;
		};

		Bounds BoundsOf(const Box& box)
		{
			glm::vec3 reach(0.0f);

			for (int axis = 0; axis < AXES; axis++)
				reach += glm::abs(box.axes[axis]) * box.half[axis];

			return { box.center - reach, box.center + reach };
		};

		Bounds BoundsOf(const Sphere& sphere)
		{
			return { sphere.center - glm::vec3(sphere.radius), sphere.center + glm::vec3(sphere.radius) };
		};

		float Radius(const Box& box, const glm::vec3& axis)
		{
			float radius = 0.0f;

			for (int i = 0; i < AXES; i++)
				radius += box.half[i] * std::abs(glm::dot(box.axes[i], axis));

			return radius;
		};

		void Add(Manifold& manifold, const glm::vec3& point, float depth)
		{
			if (manifold.count < MOST_POINTS)
			{
				manifold.points[manifold.count] = point;
				manifold.depths[manifold.count] = depth;
				manifold.count++;
			};
		};

		// Keeps the deepest point and those spread farthest from the ones
		// kept, which hold a resting face as well as all of them would.
		void AddReduced(Manifold& manifold, const Polygon& candidates)
		{
			if (candidates.count == 0)
				return;

			bool taken[MOST_CORNERS] = { };

			int deepest = 0;

			for (int i = 1; i < candidates.count; i++)
				if (candidates.depths[i] > candidates.depths[deepest])
					deepest = i;

			taken[deepest] = true;
			Add(manifold, candidates.points[deepest], candidates.depths[deepest]);

			while (manifold.count < MOST_POINTS && manifold.count < candidates.count)
			{
				int farthest = 0;
				float most = -1.0f;

				for (int i = 0; i < candidates.count; i++)
				{
					if (taken[i])
						continue;

					float nearest = INFINITE_DISTANCE;

					for (int k = 0; k < manifold.count; k++)
						nearest = std::min(nearest, glm::length(candidates.points[i] - manifold.points[k]));

					if (nearest > most)
					{
						most = nearest;
						farthest = i;
					};
				};

				taken[farthest] = true;
				Add(manifold, candidates.points[farthest], candidates.depths[farthest]);
			};
		};

		// The part of polygon on the side of the plane its normal points away from.
		Polygon Clip(const Polygon& polygon, const glm::vec3& normal, float offset)
		{
			Polygon kept;

			for (int i = 0; i < polygon.count; i++)
			{
				const glm::vec3& from = polygon.points[i];
				const glm::vec3& to = polygon.points[(i + 1) % polygon.count];

				const float fromSide = glm::dot(normal, from) - offset;
				const float toSide = glm::dot(normal, to) - offset;

				if (fromSide <= 0.0f)
					kept.Add(from);

				if ((fromSide <= 0.0f) != (toSide <= 0.0f))
					kept.Add(from + (to - from) * (fromSide / (fromSide - toSide)));
			};

			return kept;
		};

		// The face of box whose outward normal is closest to facing.
		Polygon Face(const Box& box, const glm::vec3& facing)
		{
			int axis = 0;
			float most = -1.0f;

			for (int i = 0; i < AXES; i++)
			{
				const float along = std::abs(glm::dot(box.axes[i], facing));

				if (along > most)
				{
					most = along;
					axis = i;
				};
			};

			const float side = glm::dot(box.axes[axis], facing) > 0.0f ? 1.0f : -1.0f;
			const glm::vec3 middle = box.center + box.axes[axis] * box.half[axis] * side;

			const int across = (axis + 1) % AXES;
			const int up = (axis + 2) % AXES;
			const glm::vec3 u = box.axes[across] * box.half[across];
			const glm::vec3 v = box.axes[up] * box.half[up];

			Polygon face;

			for (const glm::vec3& corner : { middle - u - v, middle + u - v, middle + u + v, middle - u + v })
				face.Add(corner);

			return face;
		};

		// Clips incident to the sides of box's face pointing along outward,
		// keeping what sits behind that face or within margin of it, each
		// point's depth how far behind.
		void ClipToFace(const Box& box, int axis, const glm::vec3& outward, Polygon incident, float margin, Polygon& points)
		{
			const glm::vec3 middle = box.center + outward * box.half[axis];

			for (int side = 1; side < AXES; side++)
			{
				const int other = (axis + side) % AXES;
				const glm::vec3& edge = box.axes[other];
				const float reach = glm::dot(edge, box.center);

				incident = Clip(incident, edge, reach + box.half[other]);
				incident = Clip(incident, -edge, -reach + box.half[other]);
			};

			for (int i = 0; i < incident.count; i++)
			{
				const glm::vec3& point = incident.points[i];
				const float behind = glm::dot(middle - point, outward);

				if (behind >= -margin)
					points.Add(point + outward * behind * HALF, behind);
			};
		};

		// The closest points between the segments from p1 to q1 and p2 to q2,
		// after Ericson's Real-Time Collision Detection.
		void ClosestBetween(const glm::vec3& p1, const glm::vec3& q1, const glm::vec3& p2, const glm::vec3& q2, glm::vec3& c1, glm::vec3& c2)
		{
			const glm::vec3 d1 = q1 - p1;
			const glm::vec3 d2 = q2 - p2;
			const glm::vec3 r = p1 - p2;

			const float a = glm::dot(d1, d1);
			const float e = glm::dot(d2, d2);
			const float f = glm::dot(d2, r);

			float s = 0.0f;
			float t = 0.0f;

			if (a <= EPSILON && e <= EPSILON)
			{
				c1 = p1;
				c2 = p2;
				return;
			};

			if (a <= EPSILON)
				t = glm::clamp(f / e, 0.0f, 1.0f);
			else
			{
				const float c = glm::dot(d1, r);

				if (e <= EPSILON)
					s = glm::clamp(-c / a, 0.0f, 1.0f);
				else
				{
					const float b = glm::dot(d1, d2);
					const float denominator = a * e - b * b;

					s = denominator > EPSILON ? glm::clamp((b * f - c * e) / denominator, 0.0f, 1.0f) : 0.0f;
					t = (b * s + f) / e;

					if (t < 0.0f)
					{
						t = 0.0f;
						s = glm::clamp(-c / a, 0.0f, 1.0f);
					}
					else if (t > 1.0f)
					{
						t = 1.0f;
						s = glm::clamp((b - c) / a, 0.0f, 1.0f);
					};
				};
			};

			c1 = p1 + d1 * s;
			c2 = p2 + d2 * t;
		};

		// The point of the triangle nearest point, after Ericson.
		glm::vec3 ClosestOn(const Triangle& triangle, const glm::vec3& point)
		{
			const glm::vec3& a = triangle[0];
			const glm::vec3& b = triangle[1];
			const glm::vec3& c = triangle[2];

			const glm::vec3 ab = b - a;
			const glm::vec3 ac = c - a;
			const glm::vec3 ap = point - a;

			const float d1 = glm::dot(ab, ap);
			const float d2 = glm::dot(ac, ap);

			if (d1 <= 0.0f && d2 <= 0.0f)
				return a;

			const glm::vec3 bp = point - b;
			const float d3 = glm::dot(ab, bp);
			const float d4 = glm::dot(ac, bp);

			if (d3 >= 0.0f && d4 <= d3)
				return b;

			const float vc = d1 * d4 - d3 * d2;

			if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
				return a + ab * (d1 / (d1 - d3));

			const glm::vec3 cp = point - c;
			const float d5 = glm::dot(ab, cp);
			const float d6 = glm::dot(ac, cp);

			if (d6 >= 0.0f && d5 <= d6)
				return c;

			const float vb = d5 * d2 - d1 * d6;

			if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
				return a + ac * (d2 / (d2 - d6));

			const float va = d3 * d6 - d5 * d4;

			if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
				return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));

			const float denominator = 1.0f / (va + vb + vc);

			return a + ab * (vb * denominator) + ac * (vc * denominator);
		};

		// Normal from the sphere into the other shape.
		void SphereSphere(const Sphere& a, const Sphere& b, float margin, std::vector<Manifold>& manifolds)
		{
			const glm::vec3 between = b.center - a.center;
			const float reach = a.radius + b.radius;
			const float distance = glm::length(between);

			if (distance >= reach + margin)
				return;

			Manifold manifold;
			manifold.normal = distance > EPSILON ? between / distance : glm::vec3(0.0f, 1.0f, 0.0f);

			const float depth = reach - distance;
			Add(manifold, a.center + manifold.normal * (a.radius - depth * HALF), depth);

			manifolds.push_back(manifold);
		};

		void SphereBox(const Sphere& sphere, const Box& box, float margin, std::vector<Manifold>& manifolds)
		{
			const glm::vec3 offset = sphere.center - box.center;

			glm::vec3 closest = box.center;
			bool inside = true;

			// The nearest face, for a centre inside the box.
			int nearestAxis = 0;
			float nearestGap = INFINITE_DISTANCE;
			float nearestSide = 1.0f;

			for (int axis = 0; axis < AXES; axis++)
			{
				const float along = glm::dot(offset, box.axes[axis]);
				const float clamped = glm::clamp(along, -box.half[axis], box.half[axis]);

				if (clamped != along)
					inside = false;

				const float gap = box.half[axis] - std::abs(along);

				if (gap < nearestGap)
				{
					nearestGap = gap;
					nearestAxis = axis;
					nearestSide = along >= 0.0f ? 1.0f : -1.0f;
				};

				closest += box.axes[axis] * clamped;
			};

			Manifold manifold;

			if (inside)
			{
				manifold.normal = -box.axes[nearestAxis] * nearestSide;
				Add(manifold, sphere.center, sphere.radius + nearestGap);
			}
			else
			{
				const glm::vec3 toBox = closest - sphere.center;
				const float distance = glm::length(toBox);

				if (distance >= sphere.radius + margin || distance < EPSILON)
					return;

				manifold.normal = toBox / distance;
				Add(manifold, closest, sphere.radius - distance);
			};

			manifolds.push_back(manifold);
		};

		void BoxBox(const Box& a, const Box& b, float margin, std::vector<Manifold>& manifolds)
		{
			const glm::vec3 between = b.center - a.center;

			float bestOverlap = INFINITE_DISTANCE;
			glm::vec3 bestAxis(0.0f);

			// 0 to 2 a's faces, 3 to 5 b's, then a's axis i crossed with b's j.
			constexpr int FACES = 2 * AXES;
			int best = -1;

			const auto test =
				[&](const glm::vec3& axis, int index)
				{
					const float distance = glm::dot(between, axis);
					const float overlap = Radius(a, axis) + Radius(b, axis) - std::abs(distance);

					if (overlap < -margin)
						return false;

					if (overlap + (index >= FACES ? FACE_SLACK : 0.0f) < bestOverlap)
					{
						bestOverlap = overlap;
						bestAxis = distance < 0.0f ? -axis : axis;
						best = index;
					};

					return true;
				};

			for (int i = 0; i < AXES; i++)
				if (!test(a.axes[i], i) || !test(b.axes[i], AXES + i))
					return;

			for (int i = 0; i < AXES; i++)
				for (int j = 0; j < AXES; j++)
				{
					glm::vec3 axis = glm::cross(a.axes[i], b.axes[j]);
					const float length = glm::length(axis);

					if (length < EPSILON)
						continue;

					if (!test(axis / length, FACES + i * AXES + j))
						return;
				};

			Manifold manifold;
			manifold.normal = bestAxis;

			Polygon points;

			if (best < AXES)
				ClipToFace(a, best, bestAxis, Face(b, -bestAxis), margin, points);
			else if (best < FACES)
				ClipToFace(b, best - AXES, -bestAxis, Face(a, bestAxis), margin, points);
			else
			{
				const int i = (best - FACES) / AXES;
				const int j = (best - FACES) % AXES;

				// Each box's edge along the crossed axis that lies nearest the other.
				const auto edge =
					[](const Box& box, int along, const glm::vec3& toward, glm::vec3& from, glm::vec3& to)
					{
						glm::vec3 middle = box.center;

						for (int k = 0; k < AXES; k++)
							if (k != along)
								middle += box.axes[k] * box.half[k] * (glm::dot(box.axes[k], toward) > 0.0f ? 1.0f : -1.0f);

						from = middle - box.axes[along] * box.half[along];
						to = middle + box.axes[along] * box.half[along];
					};

				glm::vec3 fromA, toA, fromB, toB, onA, onB;
				edge(a, i, bestAxis, fromA, toA);
				edge(b, j, -bestAxis, fromB, toB);
				ClosestBetween(fromA, toA, fromB, toB, onA, onB);

				points.Add((onA + onB) * HALF, bestOverlap);
			};

			AddReduced(manifold, points);

			if (manifold.count > 0)
				manifolds.push_back(manifold);
		};

		// Normal from the sphere into the triangle.
		void SphereTriangle(const Sphere& sphere, const Triangle& triangle, float margin, std::vector<Manifold>& manifolds)
		{
			const glm::vec3 closest = ClosestOn(triangle, sphere.center);
			const glm::vec3 toTriangle = closest - sphere.center;
			const float distance = glm::length(toTriangle);

			if (distance >= sphere.radius + margin)
				return;

			Manifold manifold;

			if (distance > EPSILON)
				manifold.normal = toTriangle / distance;
			else
			{
				const glm::vec3 face = glm::cross(triangle[1] - triangle[0], triangle[2] - triangle[0]);
				const float length = glm::length(face);

				if (length < EPSILON)
					return;

				manifold.normal = -face / length;
			};

			Add(manifold, closest, sphere.radius - distance);
			manifolds.push_back(manifold);
		};

		// Normal from the box into the triangle.
		void BoxTriangle(const Box& box, const Triangle& triangle, float margin, std::vector<Manifold>& manifolds)
		{
			const glm::vec3 edges[CORNERS] =
			{
				triangle[1] - triangle[0],
				triangle[2] - triangle[1],
				triangle[0] - triangle[2],
			};

			const glm::vec3 face = glm::cross(edges[0], -edges[2]);
			const float faceLength = glm::length(face);

			if (faceLength < EPSILON)
				return;

			const glm::vec3 faceNormal = face / faceLength;

			float bestOverlap = INFINITE_DISTANCE;
			glm::vec3 bestAxis(0.0f);

			// 0 to 2 the box's faces, 3 the triangle's, then box axis i
			// crossed with triangle edge j.
			constexpr int TRIANGLE_FACE = AXES;
			constexpr int FACES = AXES + 1;
			int best = -1;

			const auto test =
				[&](const glm::vec3& axis, int index)
				{
					const float middle = glm::dot(box.center, axis);
					const float reach = Radius(box, axis);

					float least = glm::dot(triangle[0], axis);
					float most = least;

					for (int corner = 1; corner < CORNERS; corner++)
					{
						const float along = glm::dot(triangle[corner], axis);
						least = std::min(least, along);
						most = std::max(most, along);
					};

					// How far the box would go back along -axis, or along axis, to clear it.
					const float back = middle + reach - least;
					const float forward = most - (middle - reach);

					if (back < -margin || forward < -margin)
						return false;

					const float overlap = std::min(back, forward);

					if (overlap + (index >= FACES ? FACE_SLACK : 0.0f) < bestOverlap)
					{
						bestOverlap = overlap;
						bestAxis = back < forward ? axis : -axis;
						best = index;
					};

					return true;
				};

			for (int i = 0; i < AXES; i++)
				if (!test(box.axes[i], i))
					return;

			if (!test(faceNormal, TRIANGLE_FACE))
				return;

			for (int i = 0; i < AXES; i++)
				for (int j = 0; j < CORNERS; j++)
				{
					const glm::vec3 axis = glm::cross(box.axes[i], edges[j]);
					const float length = glm::length(axis);

					if (length < EPSILON)
						continue;

					if (!test(axis / length, FACES + i * CORNERS + j))
						return;
				};

			Manifold manifold;
			manifold.normal = bestAxis;

			Polygon points;

			if (best < AXES)
			{
				const glm::vec3 outward = box.axes[best] * (glm::dot(box.axes[best], bestAxis) > 0.0f ? 1.0f : -1.0f);

				Polygon incident;

				for (const glm::vec3& corner : triangle)
					incident.Add(corner);

				ClipToFace(box, best, outward, incident, margin, points);
			}
			else if (best == TRIANGLE_FACE)
			{
				// The box's face toward the triangle, clipped to the triangle's
				// sides, kept where it has gone through the triangle's plane.
				Polygon incident = Face(box, bestAxis);
				const glm::vec3 inward = -bestAxis;

				for (int j = 0; j < CORNERS; j++)
				{
					glm::vec3 side = glm::cross(edges[j], faceNormal);

					// Pointing out of the triangle, away from its third corner.
					if (glm::dot(side, triangle[(j + 2) % CORNERS] - triangle[j]) > 0.0f)
						side = -side;

					incident = Clip(incident, side, glm::dot(side, triangle[j]));
				};

				for (int k = 0; k < incident.count; k++)
				{
					const glm::vec3& point = incident.points[k];
					const float through = glm::dot(point - triangle[0], -inward);

					if (through >= -margin)
						points.Add(point + inward * through * HALF, through);
				};
			}
			else
			{
				const int i = (best - FACES) / CORNERS;
				const int j = (best - FACES) % CORNERS;

				glm::vec3 middle = box.center;

				for (int k = 0; k < AXES; k++)
					if (k != i)
						middle += box.axes[k] * box.half[k] * (glm::dot(box.axes[k], bestAxis) > 0.0f ? 1.0f : -1.0f);

				glm::vec3 onBox, onTriangle;
				ClosestBetween(
					middle - box.axes[i] * box.half[i], middle + box.axes[i] * box.half[i],
					triangle[j], triangle[(j + 1) % CORNERS],
					onBox, onTriangle);

				points.Add((onBox + onTriangle) * HALF, bestOverlap);
			};

			AddReduced(manifold, points);

			if (manifold.count > 0)
				manifolds.push_back(manifold);
		};

		// The shapes a collider can be, posed. A mesh is its triangles.
		struct Posed
		{
			enum Kind { NONE, SPHERE, BOX, MESH } kind = NONE;

			Sphere sphere{ };
			Box box{ };
			const MeshCollider* mesh = nullptr;
			glm::mat4 world{ 1.0f };
		};

		Posed Pose(const Collider& collider, const glm::mat4& world)
		{
			Posed posed;
			posed.world = world;

			switch (collider.Shape())
			{
			case ColliderShape::Sphere:
				posed.kind = Posed::SPHERE;
				static_cast<const SphereCollider&>(collider).InWorld(world, posed.sphere.center, posed.sphere.radius);
				break;

			case ColliderShape::Box:
				if (BoxOf(static_cast<const BoxCollider&>(collider), world, posed.box))
					posed.kind = Posed::BOX;
				break;

			case ColliderShape::Mesh:
				posed.kind = Posed::MESH;
				posed.mesh = &static_cast<const MeshCollider&>(collider);
				break;
			};

			return posed;
		};

		// The manifolds of the solid against the mesh, normals from solid into mesh.
		void SolidMesh(const Posed& solid, const Posed& mesh, float margin, std::vector<Manifold>& manifolds)
		{
			const Bounds near = (solid.kind == Posed::BOX ? BoundsOf(solid.box) : BoundsOf(solid.sphere)).Expanded(margin);

			// Reused from call to call.
			thread_local std::vector<glm::vec3> corners{ };
			corners.clear();
			mesh.mesh->TrianglesNear(mesh.world, near, corners);

			for (size_t i = 0; i + CORNERS <= corners.size(); i += CORNERS)
			{
				const Triangle triangle{ corners[i], corners[i + 1], corners[i + 2] };

				if (solid.kind == Posed::BOX)
					BoxTriangle(solid.box, triangle, margin, manifolds);
				else
					SphereTriangle(solid.sphere, triangle, margin, manifolds);
			};
		};

		// Turns the manifolds from first on to run the other way.
		void Reverse(std::vector<Manifold>& manifolds, size_t first)
		{
			for (size_t i = first; i < manifolds.size(); i++)
				manifolds[i].normal = -manifolds[i].normal;
		};
	};

	void Collide(
		const Collider& a, const glm::mat4& worldA,
		const Collider& b, const glm::mat4& worldB,
		float margin,
		std::vector<Manifold>& manifolds)
	{
		const Posed first = Pose(a, worldA);
		const Posed second = Pose(b, worldB);

		const size_t before = manifolds.size();

		// Each pair is written with the simpler shape first; the ones run the
		// other way round have their normals turned after.
		bool swapped = false;

		switch (first.kind)
		{
		case Posed::SPHERE:
			if (second.kind == Posed::SPHERE)
				SphereSphere(first.sphere, second.sphere, margin, manifolds);
			else if (second.kind == Posed::BOX)
				SphereBox(first.sphere, second.box, margin, manifolds);
			else if (second.kind == Posed::MESH)
				SolidMesh(first, second, margin, manifolds);
			break;

		case Posed::BOX:
			if (second.kind == Posed::SPHERE)
			{
				SphereBox(second.sphere, first.box, margin, manifolds);
				swapped = true;
			}
			else if (second.kind == Posed::BOX)
				BoxBox(first.box, second.box, margin, manifolds);
			else if (second.kind == Posed::MESH)
				SolidMesh(first, second, margin, manifolds);
			break;

		case Posed::MESH:
			if (second.kind == Posed::SPHERE || second.kind == Posed::BOX)
			{
				SolidMesh(second, first, margin, manifolds);
				swapped = true;
			};
			break;

		default:
			break;
		};

		if (swapped)
			Reverse(manifolds, before);
	};
};
};
