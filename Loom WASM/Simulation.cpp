#include "Simulation.h"

#include "Collider.h"
#include "ColliderTree.h"
#include "GameObject.h"
#include "Jobs.h"
#include "Physics.h"
#include "Rigidbody.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/euler_angles.hpp"

#include <algorithm>
#include <cmath>


namespace Loom
{
	namespace
	{
		// As many as Unity's default.
		constexpr int ITERATIONS = 6;

		// The share of an overlap pushed out each step, and how much is
		// left alone so resting contacts do not jitter.
		constexpr float BAUMGARTE = 0.2f;
		constexpr float SLOP = 0.005f;

		// The fastest an overlap is pushed apart, so bodies started inside one
		// another separate rather than burst apart.
		constexpr float MOST_PUSH = 3.0f;

		// Slower approaches than this do not bounce, so resting bodies settle.
		constexpr float BOUNCE_SPEED = 1.0f;

		// How much further than a step's travel a pair is looked at, so a
		// body coming to rest keeps its contacts from step to step.
		constexpr float SPECULATIVE_SLACK = 0.02f;

		// Closer than this counts as touching, as Unity's contactOffset has
		// it, so a resting contact hovering about zero does not flicker
		// between touching and apart, and wake its neighbours each time.
		constexpr float CONTACT_OFFSET = 0.01f;

		// A body whose kinetic energy, over its mass, stays below this for
		// SLEEP_AFTER seconds goes to sleep, as Unity's sleepThreshold has it.
		// Spin counts by the body's inertia, so a small body spinning in
		// place, which friction cannot stop, still comes to rest.
		constexpr float SLEEP_ENERGY = 0.005f;
		constexpr float SLEEP_AFTER = 0.5f;

		// A sleeper is woken by a body striking it faster than this, well
		// above sleeping speed, so jostling neighbours do not keep a pile awake.
		constexpr float WAKE_SPEED = 0.5f;

		// How many leaves one job's chunk queries, how many candidates it
		// collides, and how many bodies it readies or moves: enough that a
		// chunk outweighs handing it out.
		constexpr size_t QUERY_GRAIN = 64;
		constexpr size_t BODY_GRAIN = 64;
		constexpr size_t PAIR_GRAIN = 64;
		constexpr size_t CONSIDER_GRAIN = 128;

		// A contact this close to one from the last step, facing nearly the
		// same way, is taken to be the same one.
		constexpr float SAME_DISTANCE = 0.05f;
		constexpr float SAME_FACING = 0.95f;

		// Of a uniform box about its centre, by its half extents; of a solid
		// sphere, by its radius.
		constexpr float BOX_INERTIA = 1.0f / 3.0f;
		constexpr float SPHERE_INERTIA = 2.0f / 5.0f;
		constexpr float HALF = 0.5f;

		glm::vec3 Perpendicular(const glm::vec3& normal)
		{
			// Crossed with an axis it is far from parallel to.
			constexpr float MOSTLY = 0.57735f;

			const glm::vec3 other = std::abs(normal.x) < MOSTLY ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

			return glm::normalize(glm::cross(normal, other));
		};

		bool Listens(const GameObject& object)
		{
			for (const ComponentBase* component : object.GetComponents())
				if (component->HearsContacts())
					return true;

			return false;
		};
	};

	Simulation::Simulation(ColliderTree& tree) :
		m_tree(tree)
	{ };

	void Simulation::Add(Rigidbody& rigidbody)
	{
		Body body{ };
		body.rigidbody = &rigidbody;
		body.object = rigidbody.m_gameObject;

		m_bodies.push_back(std::move(body));
	};

	bool Simulation::Ready(Body& body)
	{
		Rigidbody& rigidbody = *body.rigidbody;

		for (ComponentBase* component : body.object->GetComponents())
		{
			Collider* collider = dynamic_cast<Collider*>(component);

			if (collider != nullptr && collider->m_tree == &m_tree)
				body.colliders.push_back(collider);
		};

		body.dynamic = !rigidbody.isKinematic && rigidbody.mass > 0.0f;

		// Anything set on a sleeping body since it fell asleep wakes it.
		if (rigidbody.m_sleeping && (rigidbody.velocity != glm::vec3(0.0f) || rigidbody.angularVelocity != glm::vec3(0.0f)))
			rigidbody.WakeUp();

		// Asleep, it holds still against whatever touches it, and is only
		// weighed if something wakes it.
		body.awake = !rigidbody.m_sleeping;

		return !body.awake || Weigh(body);
	};

	bool Simulation::Weigh(Body& body)
	{
		Rigidbody& rigidbody = *body.rigidbody;
		// The walk has just synced its colliders, so the first already knows.
		const glm::mat4 world = body.colliders.empty() ? body.object->WorldMatrix() : body.colliders.front()->m_synced;

		body.position = glm::vec3(world[3]);

		glm::mat3 rotation(1.0f);
		glm::vec3 scale;

		for (int axis = 0; axis < glm::vec3::length(); axis++)
		{
			scale[axis] = glm::length(glm::vec3(world[axis]));

			if (scale[axis] == 0.0f)
				return false;

			rotation[axis] = glm::vec3(world[axis]) / scale[axis];
		};

		body.orientation = glm::normalize(glm::quat_cast(rotation));

		// Spread as its first box or sphere is; a body with neither is a
		// sphere of radius half a unit.
		glm::vec3 inertia(SPHERE_INERTIA * rigidbody.mass * HALF * HALF);

		if (!body.colliders.empty())
		{
			const Collider* first = body.colliders.front();

			if (first->Shape() == ColliderShape::Box)
			{
				const glm::vec3 half = glm::abs(*static_cast<const BoxCollider*>(first)->size) * scale * HALF;
				const glm::vec3 squared = half * half;

				inertia = BOX_INERTIA * rigidbody.mass * glm::vec3(
					squared.y + squared.z,
					squared.x + squared.z,
					squared.x + squared.y);
			}
			else if (first->Shape() == ColliderShape::Sphere)
			{
				glm::vec3 centre;
				float radius;
				static_cast<const SphereCollider*>(first)->InWorld(world, centre, radius);

				inertia = glm::vec3(SPHERE_INERTIA * rigidbody.mass * radius * radius);
			};
		};

		rigidbody.m_inverseInertia = body.dynamic
			? glm::vec3(
				inertia.x > 0.0f ? 1.0f / inertia.x : 0.0f,
				inertia.y > 0.0f ? 1.0f / inertia.y : 0.0f,
				inertia.z > 0.0f ? 1.0f / inertia.z : 0.0f)
			: glm::vec3(0.0f);

		const glm::mat3 turned = glm::mat3_cast(body.orientation);
		const glm::mat3 own(
			rigidbody.m_inverseInertia.x, 0.0f, 0.0f,
			0.0f, rigidbody.m_inverseInertia.y, 0.0f,
			0.0f, 0.0f, rigidbody.m_inverseInertia.z);

		body.inverseInertia = turned * own * glm::transpose(turned);
		body.inverseMass = body.dynamic ? 1.0f / rigidbody.mass : 0.0f;

		return true;
	};

	void Simulation::Step(float seconds)
	{
		m_step++;

		// Once the tree is in step, so a collider attached since the body
		// was is in it and moves with the body.
		Jobs::For(m_bodies.size(), BODY_GRAIN,
			[&](size_t, size_t begin, size_t end)
			{
				for (size_t i = begin; i < end; i++)
					if (!Ready(m_bodies[i]))
						m_bodies[i].object = nullptr;
			});

		std::erase_if(m_bodies, [](const Body& body) { return body.object == nullptr; });

		for (int index = 0; index < (int)m_bodies.size(); index++)
			for (Collider* collider : m_bodies[index].colliders)
				collider->m_body = index;

		const glm::vec3 gravity = Physics::GetGravity();

		Jobs::For(m_bodies.size(), BODY_GRAIN,
			[&](size_t, size_t begin, size_t end)
			{
				for (size_t i = begin; i < end; i++)
					Accelerate(m_bodies[i], gravity, seconds);
			});

		Gather(seconds);

		// Each pair's contacts made ready at once, from the velocities before
		// any warm start, then the warm starts applied in order.
		Jobs::For(m_pairs.size(), PAIR_GRAIN,
			[&](size_t, size_t begin, size_t end)
			{
				for (size_t i = begin; i < end; i++)
					Prepare(m_pairs[i], seconds);
			});

		for (Pair& pair : m_pairs)
			WarmStart(pair);

		for (int iteration = 0; iteration < ITERATIONS; iteration++)
			for (Pair& pair : m_pairs)
				Solve(pair);

		// A body with others hanging under it moves first, outermost first,
		// so those under it are placed from where it ends up; the rest move at
		// once, then their leaves are placed.
		m_parents.clear();

		for (Body& body : m_bodies)
		{
			body.rigidbody->velocity = body.velocity;
			body.rigidbody->angularVelocity = body.angularVelocity;

			if (!body.object->GetChildren().empty())
				m_parents.push_back(&body);
		};

		std::sort(m_parents.begin(), m_parents.end(),
			[](const Body* a, const Body* b) { return Depth(*a->object) < Depth(*b->object); });

		for (Body* parent : m_parents)
		{
			Integrate(*parent, seconds);
			Place(*parent, seconds);
		};

		Jobs::For(m_bodies.size(), BODY_GRAIN,
			[&](size_t, size_t begin, size_t end)
			{
				for (size_t i = begin; i < end; i++)
					if (m_bodies[i].object->GetChildren().empty())
						Integrate(m_bodies[i], seconds);
			});

		for (Body& body : m_bodies)
			if (body.object->GetChildren().empty())
				Place(body, seconds);

		Report();

		for (const Body& body : m_bodies)
			for (Collider* collider : body.colliders)
				collider->m_body = NONE;

		m_bodies.clear();
	};

	Simulation::Key Simulation::Ordered(const Collider* a, const Collider* b)
	{
		return a < b ? Key{ a, b } : Key{ b, a };
	};

	void Simulation::Accelerate(Body& body, const glm::vec3& gravity, float seconds)
	{
		Rigidbody& rigidbody = *body.rigidbody;

		if (body.awake && body.dynamic)
		{
			rigidbody.velocity += (rigidbody.m_force + (rigidbody.useGravity ? gravity : glm::vec3(0.0f))) * seconds;
			rigidbody.angularVelocity += (body.inverseInertia * rigidbody.m_torque + rigidbody.m_spin) * seconds
				+ body.inverseInertia * rigidbody.m_angularImpulse;

			rigidbody.velocity /= 1.0f + seconds * rigidbody.drag;
			rigidbody.angularVelocity /= 1.0f + seconds * rigidbody.angularDrag;
		};

		rigidbody.m_force = glm::vec3(0.0f);
		rigidbody.m_torque = glm::vec3(0.0f);
		rigidbody.m_spin = glm::vec3(0.0f);
		rigidbody.m_angularImpulse = glm::vec3(0.0f);

		body.velocity = rigidbody.velocity;
		body.angularVelocity = rigidbody.angularVelocity;
	};

	void Simulation::Forget(const Collider& collider)
	{
		const auto involves =
			[&](const Key& key) { return key.first == &collider || key.second == &collider; };

		std::erase_if(m_candidates, involves);
		std::erase_if(m_candidateSet, involves);

		std::erase_if(m_touching,
			[&](const auto& entry)
			{
				const bool first = entry.first.first == &collider;

				if (!first && entry.first.second != &collider)
					return false;

				// What it held up has to be free to fall.
				WakeOwner(*(first ? entry.first.second : entry.first.first));

				return true;
			});
	};

	void Simulation::WakeOwner(const Collider& collider)
	{
		if (Rigidbody* rigidbody = collider.m_gameObject->GetComponent<Rigidbody>())
			rigidbody->WakeUp();
	};

	int Simulation::BodyOf(const Collider& collider) const
	{
		return collider.m_body;
	};

	Simulation::Body& Simulation::At(int body)
	{
		return body == NONE ? m_ground : m_bodies[body];
	};

	glm::vec3 Simulation::VelocityAt(int body, const glm::vec3& point)
	{
		const Body& moving = At(body);

		return moving.velocity + glm::cross(moving.angularVelocity, point - moving.position);
	};

	void Simulation::Wake(int body)
	{
		Body& sleeper = m_bodies[body];

		if (!Weigh(sleeper))
			return;

		sleeper.rigidbody->WakeUp();
		sleeper.awake = true;
	};

	void Simulation::Gather(float seconds)
	{
		m_pairs.clear();
		m_contacts.clear();

		// Each moving body's leaf reaches along the travel it is about to
		// make, which a velocity set or changed since the last step may not.
		for (Body& body : m_bodies)
		{
			body.travel = glm::length(body.velocity) * seconds;

			if (body.awake)
				for (const Collider* collider : body.colliders)
					m_tree.Reach(*collider, body.velocity * seconds);
		};

		// New candidates: each leaf put in or refitted since the last step,
		// against whatever it now overlaps, found in parallel and taken in
		// chunk order. Leaves cover their bodies' travel, so two that can
		// meet this step overlap.
		const std::vector<Collider*> moved = m_tree.TakeMoved();

		m_found.resize(std::max(m_found.size(), Jobs::Chunks(moved.size(), QUERY_GRAIN)));

		Jobs::For(moved.size(), QUERY_GRAIN,
			[&](size_t chunk, size_t begin, size_t end)
			{
				std::vector<Key>& found = m_found[chunk];
				found.clear();

				for (size_t i = begin; i < end; i++)
				{
					const Collider* collider = moved[i];
					const Bounds* padded = m_tree.PaddedBounds(*collider);

					if (padded == nullptr)
						continue;

					m_tree.Query(*padded,
						[&](Collider& other)
						{
							if (&other != collider)
								found.push_back(Ordered(collider, &other));
						});
				};
			});

		for (size_t chunk = 0; chunk < Jobs::Chunks(moved.size(), QUERY_GRAIN); chunk++)
			for (const Key& key : m_found[chunk])
				if (m_candidateSet.insert(key).second)
					m_candidates.push_back(key);

		// Candidates whose leaves have parted are dropped, and the rest to be
		// looked at gathered. Where neither side is awake, nothing has moved
		// to part them.
		m_live.clear();

		for (size_t i = 0; i < m_candidates.size(); )
		{
			const Key key = m_candidates[i];

			if (!Awake(BodyOf(*key.first)) && !Awake(BodyOf(*key.second)))
			{
				i++;
				continue;
			};

			const Bounds* first = m_tree.PaddedBounds(*key.first);
			const Bounds* second = m_tree.PaddedBounds(*key.second);

			if (first == nullptr || second == nullptr || !first->Overlaps(*second))
			{
				m_candidateSet.erase(key);
				m_candidates[i] = m_candidates.back();
				m_candidates.pop_back();
				continue;
			};

			m_live.push_back(key);
			i++;
		};

		// Collided in parallel, each chunk into its own slot, then put
		// together in chunk order, with the sleepers they wake woken after.
		const size_t chunks = Jobs::Chunks(m_live.size(), CONSIDER_GRAIN);
		m_considered.resize(std::max(m_considered.size(), chunks));

		Jobs::For(m_live.size(), CONSIDER_GRAIN,
			[&](size_t chunk, size_t begin, size_t end)
			{
				Considered& out = m_considered[chunk];
				out.pairs.clear();
				out.contacts.clear();
				out.wakes.clear();

				for (size_t i = begin; i < end; i++)
					Consider(*const_cast<Collider*>(m_live[i].first), *const_cast<Collider*>(m_live[i].second), out);
			});

		for (size_t chunk = 0; chunk < chunks; chunk++)
		{
			const Considered& out = m_considered[chunk];
			const size_t base = m_contacts.size();

			m_contacts.insert(m_contacts.end(), out.contacts.begin(), out.contacts.end());

			for (Pair pair : out.pairs)
			{
				pair.first += base;
				m_pairs.push_back(pair);
			};

			for (int sleeper : out.wakes)
				if (!m_bodies[sleeper].awake)
					Wake(sleeper);
		};
	};

	bool Simulation::Awake(int body) const
	{
		return body != NONE && m_bodies[body].awake;
	};

	void Simulation::Consider(Collider& a, Collider& b, Considered& out) const
	{
		const auto dynamic =
			[&](int body) { return body != NONE && m_bodies[body].dynamic; };

		const auto travel =
			[&](int body) { return body == NONE ? 0.0f : m_bodies[body].travel; };

		Pair pair{ };
		pair.a = &a;
		pair.b = &b;
		pair.bodyA = BodyOf(a);
		pair.bodyB = BodyOf(b);

		// Nothing to do between parts of one body.
		if (pair.bodyA == pair.bodyB)
			return;

		// A MeshCollider moves nothing on a body that can be pushed.
		if ((dynamic(pair.bodyA) && a.Shape() == ColliderShape::Mesh) || (dynamic(pair.bodyB) && b.Shape() == ColliderShape::Mesh))
			return;

		pair.trigger = a.IsTrigger() || b.IsTrigger();

		if (!pair.trigger && !dynamic(pair.bodyA) && !dynamic(pair.bodyB))
			return;

		const float margin = pair.trigger ? 0.0f : travel(pair.bodyA) + travel(pair.bodyB) + SPECULATIVE_SLACK;

		// Their own boxes still too far apart to touch this step.
		if (!a.m_bounds.Expanded(margin).Overlaps(b.m_bounds))
			return;

		out.manifolds.clear();
		Contacts::Collide(a.m_shape, b.m_shape, margin, out.manifolds);

		pair.first = out.contacts.size();

		for (const Contacts::Manifold& manifold : out.manifolds)
			for (int i = 0; i < manifold.count; i++)
			{
				Contact contact{ };
				contact.point = manifold.points[i];
				contact.normal = manifold.normal;
				contact.depth = manifold.depths[i];

				pair.touching |= contact.depth >= -CONTACT_OFFSET;
				out.contacts.push_back(contact);
			};

		pair.count = out.contacts.size() - pair.first;

		if (pair.count == 0 || (pair.trigger && !pair.touching))
		{
			out.contacts.resize(pair.first);
			return;
		};

		const Rigidbody* first = pair.bodyA == NONE ? nullptr : m_bodies[pair.bodyA].rigidbody;
		const Rigidbody* second = pair.bodyB == NONE ? nullptr : m_bodies[pair.bodyB].rigidbody;

		if (first && second)
		{
			pair.friction = std::sqrt(first->friction * second->friction);
			pair.bounciness = std::max(*first->bounciness, *second->bounciness);
		}
		else
		{
			const Rigidbody* only = first ? first : second;
			pair.friction = only->friction;
			pair.bounciness = only->bounciness;
		};

		// A sleeper wakes when struck hard enough, or touched by something
		// that will not stop for it.
		if (!pair.trigger)
			for (const auto& [striker, sleeper] : { std::pair{ pair.bodyA, pair.bodyB }, std::pair{ pair.bodyB, pair.bodyA } })
				if (Awake(striker) && sleeper != NONE && !m_bodies[sleeper].awake
					&& (glm::length(m_bodies[striker].velocity) > WAKE_SPEED || (pair.touching && !dynamic(striker))))
					out.wakes.push_back(sleeper);

		out.pairs.push_back(pair);
	};

	void Simulation::Prepare(Pair& pair, float seconds)
	{
		if (pair.trigger)
			return;

		const Body& a = pair.bodyA == NONE ? m_ground : m_bodies[pair.bodyA];
		const Body& b = pair.bodyB == NONE ? m_ground : m_bodies[pair.bodyB];

		const auto found = m_touching.find({ pair.a, pair.b });
		const std::vector<Contact>* before = found == m_touching.end() ? nullptr : &found->second.contacts;

		for (size_t i = pair.first; i < pair.first + pair.count; i++)
		{
			Contact& contact = m_contacts[i];

			const glm::vec3 fromA = contact.point - a.position;
			const glm::vec3 fromB = contact.point - b.position;

			const glm::vec3 tangent = Perpendicular(contact.normal);
			const glm::vec3 directions[DIRECTIONS] = { contact.normal, tangent, glm::cross(contact.normal, tangent) };

			// What a push along each direction does to each side, worked out
			// once here so the iterations need only dot products.
			for (int d = 0; d < DIRECTIONS; d++)
			{
				Axis& axis = contact.axes[d];

				axis.direction = directions[d];
				axis.angularA = glm::cross(fromA, axis.direction);
				axis.angularB = glm::cross(fromB, axis.direction);
				axis.turnA = a.inverseInertia * axis.angularA;
				axis.turnB = b.inverseInertia * axis.angularB;

				const float inverse = a.inverseMass + b.inverseMass
					+ glm::dot(axis.angularA, axis.turnA) + glm::dot(axis.angularB, axis.turnB);

				axis.mass = inverse > 0.0f ? 1.0f / inverse : 0.0f;
				axis.impulse = 0.0f;
			};

			const float closing = -Speed(a, b, contact.axes[NORMAL]);
			const bool bouncing = pair.bounciness > 0.0f && closing > BOUNCE_SPEED;

			if (contact.depth >= 0.0f)
			{
				const float push = std::min(BAUMGARTE / seconds * std::max(contact.depth - SLOP, 0.0f), MOST_PUSH);
				contact.bias = std::max(push, bouncing ? pair.bounciness * closing : 0.0f);
			}
			else
			{
				// Still apart: free to close the gap and no faster, or to bounce
				// off early if it would meet within the step.
				const float gap = -contact.depth;
				contact.bias = bouncing && closing * seconds > gap ? pair.bounciness * closing : -gap / seconds;
			};

			if (before)
				for (const Contact& old : *before)
					if (glm::length(old.point - contact.point) < SAME_DISTANCE && glm::dot(old.normal, contact.normal) > SAME_FACING)
					{
						// The tangents are the same two, the normal being so close.
						for (int d = 0; d < DIRECTIONS; d++)
							contact.axes[d].impulse = old.axes[d].impulse;

						break;
					};

		};
	};

	void Simulation::WarmStart(Pair& pair)
	{
		if (pair.trigger)
			return;

		Body& a = At(pair.bodyA);
		Body& b = At(pair.bodyB);

		for (size_t i = pair.first; i < pair.first + pair.count; i++)
			for (const Axis& axis : m_contacts[i].axes)
				Push(a, b, axis, axis.impulse);
	};

	float Simulation::Speed(const Body& a, const Body& b, const Axis& axis)
	{
		return glm::dot(b.velocity - a.velocity, axis.direction)
			+ glm::dot(b.angularVelocity, axis.angularB)
			- glm::dot(a.angularVelocity, axis.angularA);
	};

	void Simulation::Push(Body& a, Body& b, const Axis& axis, float impulse)
	{
		a.velocity -= axis.direction * (impulse * a.inverseMass);
		a.angularVelocity -= axis.turnA * impulse;

		b.velocity += axis.direction * (impulse * b.inverseMass);
		b.angularVelocity += axis.turnB * impulse;
	};

	void Simulation::Solve(Pair& pair)
	{
		if (pair.trigger)
			return;

		Body& a = At(pair.bodyA);
		Body& b = At(pair.bodyB);

		for (size_t i = pair.first; i < pair.first + pair.count; i++)
		{
			Contact& contact = m_contacts[i];
			Axis& normal = contact.axes[NORMAL];

			// Friction first, held within what the normal impulse allows.
			const float limit = pair.friction * normal.impulse;

			for (int d = NORMAL + 1; d < DIRECTIONS; d++)
			{
				Axis& axis = contact.axes[d];

				const float total = glm::clamp(axis.impulse - axis.mass * Speed(a, b, axis), -limit, limit);

				Push(a, b, axis, total - axis.impulse);
				axis.impulse = total;
			};

			const float total = std::max(normal.impulse + normal.mass * (contact.bias - Speed(a, b, normal)), 0.0f);

			Push(a, b, normal, total - normal.impulse);
			normal.impulse = total;
		};
	};

	void Simulation::Integrate(Body& body, float seconds)
	{
		Rigidbody& rigidbody = *body.rigidbody;

		if (!body.awake)
			return;

		// Only what moved is written back, so a body set still, a kinematic
		// one moved by its script especially, keeps the pose it was given.
		const bool moving = body.velocity != glm::vec3(0.0f);
		const bool turning = body.angularVelocity != glm::vec3(0.0f);

		if (moving || turning)
		{
			body.position += body.velocity * seconds;

			const glm::quat spin(0.0f, body.angularVelocity);
			body.orientation = glm::normalize(body.orientation + spin * body.orientation * (HALF * seconds));

			// Back into its parent's space, keeping its own scale.
			const GameObject* parent = body.object->GetParent();
			const glm::mat4 parentInverse = parent ? glm::inverse(parent->WorldMatrix()) : glm::mat4(1.0f);

			const glm::mat4 local = parentInverse
				* glm::translate(glm::mat4(1.0f), body.position)
				* glm::mat4_cast(body.orientation);

			if (moving)
				body.object->transform.position = glm::vec3(local[3]);

			if (turning)
			{
				glm::mat4 turned(1.0f);

				for (int axis = 0; axis < glm::vec3::length(); axis++)
					turned[axis] = glm::vec4(glm::normalize(glm::vec3(local[axis])), 0.0f);

				// Transform turns about Y, then X, then Z.
				glm::vec3 radians;
				glm::extractEulerAngleYXZ(turned, radians.y, radians.x, radians.z);

				body.object->transform.rotation = glm::degrees(radians);
			};

			// From the transform just written, exactly as the next walk will
			// find it, so that walk leaves the leaf alone.
			for (Collider* collider : body.colliders)
				m_tree.Measure(*collider);

			body.moved = true;
		};

		if (body.inverseMass == 0.0f)
			return;

		// The spin's energy about the body's own axes, its inertia being the
		// inverse of what the solver turns it by.
		const glm::vec3 spinOwn = glm::transpose(glm::mat3_cast(body.orientation)) * body.angularVelocity;
		float spinning = 0.0f;

		for (int axis = 0; axis < glm::vec3::length(); axis++)
			if (rigidbody.m_inverseInertia[axis] > 0.0f)
				spinning += spinOwn[axis] * spinOwn[axis] / rigidbody.m_inverseInertia[axis];

		const float energy = HALF * (glm::dot(body.velocity, body.velocity) + spinning * body.inverseMass);

		if (energy < SLEEP_ENERGY)
		{
			rigidbody.m_still += seconds;

			if (rigidbody.m_still >= SLEEP_AFTER)
				rigidbody.Sleep();
		}
		else
			rigidbody.m_still = 0.0f;
	};

	void Simulation::Place(Body& body, float seconds)
	{
		if (!body.moved)
			return;

		// Stretched along the travel, so the next steps' motion stays inside.
		for (Collider* collider : body.colliders)
			m_tree.Place(*collider, true, body.velocity * seconds);

		body.moved = false;
	};

	int Simulation::Depth(const GameObject& object)
	{
		int depth = 0;

		for (const GameObject* above = object.GetParent(); above != nullptr; above = above->GetParent())
			depth++;

		return depth;
	};

	void Simulation::Report()
	{
		const auto rigidbodyOf =
			[&](int body) { return body == NONE ? nullptr : m_bodies[body].rigidbody; };

		// Every component on the collider's GameObject, from a copy: one may
		// attach another.
		const auto tell =
			[](Collider& collider, const auto& call)
			{
				const std::vector<ComponentBase*> components = collider.m_gameObject->GetComponents();

				for (ComponentBase* component : components)
					call(*component);
			};

		enum Phase { ENTERING, STAYING, LEAVING };

		const auto report =
			[&](Collider& a, Collider& b, int bodyA, int bodyB, bool trigger, const Contact* contacts, size_t count, Phase phase)
			{
				const bool aListens = Listens(*a.m_gameObject);
				const bool bListens = Listens(*b.m_gameObject);

				if (trigger)
				{
					const auto triggered =
						[&](Collider& own, Collider& other)
						{
							tell(own, [&](ComponentBase& component)
								{
									switch (phase)
									{
									case ENTERING:	component.OnTriggerEnter(other);	break;
									case STAYING:	component.OnTriggerStay(other);		break;
									case LEAVING:	component.OnTriggerExit(other);		break;
									};
								});
						};

					if (aListens)
						triggered(a, b);

					if (bListens)
						triggered(b, a);

					return;
				};

				// What one side is told of the other: normals out of the other one.
				const auto collided =
					[&](Collider& own, int ownBody, Collider& other, int otherBody, float facing)
					{
						Collision told{ };
						told.collider = &other;
						told.gameObject = other.m_gameObject;
						told.rigidbody = rigidbodyOf(otherBody);

						float pushed = 0.0f;

						for (size_t i = 0; i < count; i++)
						{
							told.contacts.push_back({ contacts[i].point, contacts[i].normal * facing, -contacts[i].depth });
							pushed += contacts[i].axes[NORMAL].impulse;
						};

						if (count > 0)
						{
							told.relativeVelocity = VelocityAt(otherBody, contacts[0].point) - VelocityAt(ownBody, contacts[0].point);
							told.impulse = contacts[0].normal * facing * pushed;
						};

						tell(own, [&](ComponentBase& component)
							{
								switch (phase)
								{
								case ENTERING:	component.OnCollisionEnter(told);	break;
								case STAYING:	component.OnCollisionStay(told);	break;
								case LEAVING:	component.OnCollisionExit(told);	break;
								};
							});
					};

				if (aListens)
					collided(a, bodyA, b, bodyB, -1.0f);

				if (bListens)
					collided(b, bodyB, a, bodyA, 1.0f);
			};

		for (const Pair& pair : m_pairs)
		{
			if (!pair.touching)
				continue;

			const auto [entry, entering] = m_touching.try_emplace({ pair.a, pair.b });
			Touching& touching = entry->second;

			touching.trigger = pair.trigger;
			touching.contacts.assign(m_contacts.begin() + pair.first, m_contacts.begin() + pair.first + pair.count);
			touching.step = m_step;

			report(*pair.a, *pair.b, pair.bodyA, pair.bodyB, pair.trigger, &m_contacts[pair.first], pair.count, entering ? ENTERING : STAYING);
		};

		const auto awake =
			[&](const Collider* collider)
			{
				const int body = BodyOf(*collider);
				return body != NONE && m_bodies[body].awake;
			};

		for (auto entry = m_touching.begin(); entry != m_touching.end(); )
		{
			Touching& touching = entry->second;

			if (touching.step == m_step)
			{
				++entry;
				continue;
			};

			// Asleep on both sides, it is still touching; it was only not looked at.
			if (!awake(entry->first.first) && !awake(entry->first.second))
			{
				touching.step = m_step;
				++entry;
				continue;
			};

			Collider* a = const_cast<Collider*>(entry->first.first);
			Collider* b = const_cast<Collider*>(entry->first.second);

			if (!touching.trigger)
			{
				WakeOwner(*a);
				WakeOwner(*b);
			};

			report(*a, *b, BodyOf(*a), BodyOf(*b), touching.trigger, nullptr, 0, LEAVING);

			entry = m_touching.erase(entry);
		};
	};
};
