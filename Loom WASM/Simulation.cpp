#include "Simulation.h"

#include "Collider.h"
#include "ColliderTree.h"
#include "GameObject.h"
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

		// A body whose kinetic energy, over its mass, stays below this for
		// SLEEP_AFTER seconds goes to sleep, as Unity's sleepThreshold has it.
		// Spin counts by the body's inertia, so a small body spinning in
		// place, which friction cannot stop, still comes to rest.
		constexpr float SLEEP_ENERGY = 0.005f;
		constexpr float SLEEP_AFTER = 0.5f;

		// A sleeper is woken by a body striking it faster than this, well
		// above sleeping speed, so jostling neighbours do not keep a pile awake.
		constexpr float WAKE_SPEED = 0.5f;

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
		GameObject* object = body.object;
		const glm::mat4 world = object->WorldMatrix();

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

		const bool dynamic = !rigidbody.isKinematic && rigidbody.mass > 0.0f;
		body.baseInverseMass = dynamic ? 1.0f / rigidbody.mass : 0.0f;

		// Spread as its first box or sphere is; a body with neither is a
		// sphere of radius half a unit.
		glm::vec3 inertia(SPHERE_INERTIA * rigidbody.mass * HALF * HALF);

		for (ComponentBase* component : object->GetComponents())
		{
			Collider* collider = dynamic_cast<Collider*>(component);

			if (collider == nullptr || collider->m_tree != &m_tree)
				continue;

			if (body.colliders.empty())
			{
				if (collider->Shape() == ColliderShape::Box)
				{
					const glm::vec3 half = glm::abs(*static_cast<BoxCollider*>(collider)->size) * scale * HALF;
					const glm::vec3 squared = half * half;

					inertia = BOX_INERTIA * rigidbody.mass * glm::vec3(
						squared.y + squared.z,
						squared.x + squared.z,
						squared.x + squared.y);
				}
				else if (collider->Shape() == ColliderShape::Sphere)
				{
					glm::vec3 centre;
					float radius;
					static_cast<SphereCollider*>(collider)->InWorld(world, centre, radius);

					inertia = glm::vec3(SPHERE_INERTIA * rigidbody.mass * radius * radius);
				};
			};

			body.colliders.push_back(collider);
		};

		rigidbody.m_inverseInertia = dynamic
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

		body.baseInverseInertia = turned * own * glm::transpose(turned);

		// Anything set on a sleeping body since it fell asleep wakes it.
		if (rigidbody.m_sleeping && (rigidbody.velocity != glm::vec3(0.0f) || rigidbody.angularVelocity != glm::vec3(0.0f)))
			rigidbody.WakeUp();

		// Asleep, it holds still against whatever touches it until woken.
		body.awake = !rigidbody.m_sleeping;
		body.inverseMass = body.awake ? body.baseInverseMass : 0.0f;
		body.inverseInertia = body.awake ? body.baseInverseInertia : glm::mat3(0.0f);

		return true;
	};

	void Simulation::Step(float seconds)
	{
		m_step++;

		// Once the tree is in step, so a collider attached since the body
		// was is in it and moves with the body.
		for (Body& body : m_bodies)
			if (!Ready(body))
				body.object = nullptr;

		std::erase_if(m_bodies, [](const Body& body) { return body.object == nullptr; });

		for (int index = 0; index < (int)m_bodies.size(); index++)
			for (Collider* collider : m_bodies[index].colliders)
				collider->m_body = index;

		const glm::vec3 gravity = Physics::GetGravity();

		for (Body& body : m_bodies)
		{
			Rigidbody& rigidbody = *body.rigidbody;

			if (body.awake && body.inverseMass > 0.0f)
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

		Gather(seconds);

		for (Pair& pair : m_pairs)
			Prepare(pair, seconds);

		for (int iteration = 0; iteration < ITERATIONS; iteration++)
			for (Pair& pair : m_pairs)
				Solve(pair);

		for (Body& body : m_bodies)
		{
			body.rigidbody->velocity = body.velocity;
			body.rigidbody->angularVelocity = body.angularVelocity;

			Integrate(body, seconds);
		};

		Report();

		for (const Body& body : m_bodies)
			for (Collider* collider : body.colliders)
				collider->m_body = NONE;

		m_bodies.clear();
	};

	void Simulation::Forget(const Collider& collider)
	{
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

	glm::vec3 Simulation::VelocityAt(int body, const glm::vec3& point) const
	{
		if (body == NONE)
			return glm::vec3(0.0f);

		const Body& moving = m_bodies[body];

		return moving.velocity + glm::cross(moving.angularVelocity, point - moving.position);
	};

	void Simulation::Wake(int body)
	{
		Body& sleeper = m_bodies[body];

		sleeper.rigidbody->WakeUp();
		sleeper.awake = true;
		sleeper.inverseMass = sleeper.baseInverseMass;
		sleeper.inverseInertia = sleeper.baseInverseInertia;
	};

	void Simulation::Gather(float seconds)
	{
		m_pairs.clear();
		m_contacts.clear();
		m_found.clear();

		const auto dynamic =
			[&](int body) { return body != NONE && m_bodies[body].baseInverseMass > 0.0f; };

		// A MeshCollider moves nothing on a body that can be pushed.
		const auto collides =
			[&](const Collider& collider, int body)
			{
				return !dynamic(body) || collider.Shape() != ColliderShape::Mesh;
			};

		const auto travel =
			[&](int body) { return body == NONE ? 0.0f : glm::length(m_bodies[body].velocity) * seconds; };

		for (int index = 0; index < (int)m_bodies.size(); index++)
		{
			if (!m_bodies[index].awake)
				continue;

			const float reach = travel(index);

			for (Collider* collider : m_bodies[index].colliders)
			{
				const Bounds* padded = m_tree.PaddedBounds(*collider);

				if (padded == nullptr || !collides(*collider, index))
					continue;

				m_tree.Query(padded->Expanded(reach),
					[&](Collider& other)
					{
						const int otherBody = BodyOf(other);

						if (&other == collider || otherBody == index || !collides(other, otherBody))
							return;

						Pair pair{ };
						pair.a = collider;
						pair.b = &other;
						pair.bodyA = index;
						pair.bodyB = otherBody;

						if (pair.a > pair.b)
						{
							std::swap(pair.a, pair.b);
							std::swap(pair.bodyA, pair.bodyB);
						};

						// Each pair once, whichever body's reach found it.
						if (!m_found.insert({ pair.a, pair.b }).second)
							return;

						pair.trigger = collider->IsTrigger() || other.IsTrigger();

						if (!pair.trigger && !dynamic(index) && !dynamic(otherBody))
							return;

						const float margin = pair.trigger ? 0.0f : travel(pair.bodyA) + travel(pair.bodyB) + SPECULATIVE_SLACK;

						m_manifolds.clear();
						Contacts::Collide(*pair.a, pair.a->m_synced, *pair.b, pair.b->m_synced, margin, m_manifolds);

						pair.first = m_contacts.size();

						for (const Contacts::Manifold& manifold : m_manifolds)
							for (int i = 0; i < manifold.count; i++)
							{
								Contact contact{ };
								contact.point = manifold.points[i];
								contact.normal = manifold.normal;
								contact.depth = manifold.depths[i];

								pair.touching |= contact.depth >= 0.0f;
								m_contacts.push_back(contact);
							};

						pair.count = m_contacts.size() - pair.first;

						if (pair.count == 0 || (pair.trigger && !pair.touching))
						{
							m_contacts.resize(pair.first);
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

						// A sleeper wakes when struck hard enough, or touched by
						// something that will not stop for it.
						const bool striking = glm::length(m_bodies[index].velocity) > WAKE_SPEED
							|| (pair.touching && !dynamic(index));

						if (otherBody != NONE && !m_bodies[otherBody].awake && !pair.trigger && striking)
							Wake(otherBody);

						m_pairs.push_back(pair);
					});
			};
		};
	};

	void Simulation::Prepare(Pair& pair, float seconds)
	{
		if (pair.trigger)
			return;

		const Body* a = pair.bodyA == NONE ? nullptr : &m_bodies[pair.bodyA];
		const Body* b = pair.bodyB == NONE ? nullptr : &m_bodies[pair.bodyB];

		const auto found = m_touching.find({ pair.a, pair.b });
		const std::vector<Contact>* before = found == m_touching.end() ? nullptr : &found->second.contacts;

		for (size_t i = pair.first; i < pair.first + pair.count; i++)
		{
			Contact& contact = m_contacts[i];

			contact.fromA = a ? contact.point - a->position : glm::vec3(0.0f);
			contact.fromB = b ? contact.point - b->position : glm::vec3(0.0f);

			// The impulse it takes to change the speed along a direction by one.
			const auto massAlong =
				[&](const glm::vec3& direction)
				{
					float inverse = 0.0f;

					if (a)
						inverse += a->inverseMass + glm::dot(direction, glm::cross(a->inverseInertia * glm::cross(contact.fromA, direction), contact.fromA));

					if (b)
						inverse += b->inverseMass + glm::dot(direction, glm::cross(b->inverseInertia * glm::cross(contact.fromB, direction), contact.fromB));

					return inverse > 0.0f ? 1.0f / inverse : 0.0f;
				};

			contact.normalMass = massAlong(contact.normal);
			contact.tangents[0] = Perpendicular(contact.normal);
			contact.tangents[1] = glm::cross(contact.normal, contact.tangents[0]);

			for (int k = 0; k < TANGENTS; k++)
				contact.tangentMass[k] = massAlong(contact.tangents[k]);

			const float closing = -glm::dot(VelocityAt(pair.bodyB, contact.point) - VelocityAt(pair.bodyA, contact.point), contact.normal);
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
						contact.normalImpulse = old.normalImpulse;

						for (int k = 0; k < TANGENTS; k++)
							contact.tangentImpulse[k] = old.tangentImpulse[k];

						break;
					};

			glm::vec3 impulse = contact.normal * contact.normalImpulse;

			for (int k = 0; k < TANGENTS; k++)
				impulse += contact.tangents[k] * contact.tangentImpulse[k];

			Push(pair, contact, impulse);
		};
	};

	void Simulation::Push(const Pair& pair, const Contact& contact, const glm::vec3& impulse)
	{
		if (pair.bodyA != NONE)
		{
			Body& a = m_bodies[pair.bodyA];
			a.velocity -= impulse * a.inverseMass;
			a.angularVelocity -= a.inverseInertia * glm::cross(contact.fromA, impulse);
		};

		if (pair.bodyB != NONE)
		{
			Body& b = m_bodies[pair.bodyB];
			b.velocity += impulse * b.inverseMass;
			b.angularVelocity += b.inverseInertia * glm::cross(contact.fromB, impulse);
		};
	};

	void Simulation::Solve(Pair& pair)
	{
		if (pair.trigger)
			return;

		for (size_t i = pair.first; i < pair.first + pair.count; i++)
		{
			Contact& contact = m_contacts[i];

			const auto relative =
				[&]() { return VelocityAt(pair.bodyB, contact.point) - VelocityAt(pair.bodyA, contact.point); };

			// Friction first, held within what the normal impulse allows.
			for (int k = 0; k < TANGENTS; k++)
			{
				const float limit = pair.friction * contact.normalImpulse;
				const float wanted = -contact.tangentMass[k] * glm::dot(relative(), contact.tangents[k]);
				const float total = glm::clamp(contact.tangentImpulse[k] + wanted, -limit, limit);

				Push(pair, contact, contact.tangents[k] * (total - contact.tangentImpulse[k]));
				contact.tangentImpulse[k] = total;
			};

			const float wanted = contact.normalMass * (contact.bias - glm::dot(relative(), contact.normal));
			const float total = std::max(contact.normalImpulse + wanted, 0.0f);

			Push(pair, contact, contact.normal * (total - contact.normalImpulse));
			contact.normalImpulse = total;
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
			const glm::mat4 parentWorld = parent ? parent->WorldMatrix() : glm::mat4(1.0f);
			const glm::mat4 local = glm::inverse(parentWorld)
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

			for (Collider* collider : body.colliders)
				m_tree.Sync(*collider);
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
							pushed += contacts[i].normalImpulse;
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
