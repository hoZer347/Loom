#pragma once

#include "Collision.h"
#include "Contacts.h"

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


namespace Loom
{
	struct Collider;
	struct ColliderTree;
	struct GameObject;
	struct Rigidbody;

	/**
	* Loom::Simulation
	* - A scene's rigid bodies, stepped by Scene::Physics once the collider
	*   tree is in step: forces and gravity, then contacts between the
	*   colliders of awake bodies and whatever they overlap, solved by
	*   sequential impulses with friction and bounce, then the new poses
	*   written back into the bodies' transforms
	* - A pair still apart but close enough to meet within the step gets a
	*   contact that only stops it closing faster than the gap, so a fast body
	*   cannot pass through a thin one between steps
	* - Contacts that carry over from the last step start from the impulses
	*   they ended it with, which is what lets a stack stand still
	* - Reports pairs that start, keep on or stop touching to both sides'
	*   components; a pair asleep keeps touching without being told so again
	*/
	struct Simulation final
	{
		explicit Simulation(ColliderTree& tree);

		Simulation(const Simulation&) = delete;
		Simulation& operator=(const Simulation&) = delete;

		// A body joining this step, from its OnPhysics.
		void Add(Rigidbody& rigidbody);

		void Step(float seconds);

		// A collider leaving its tree, so no pair outlives it.
		void Forget(const Collider& collider);

		// The simulation of the scene being stepped.
		static inline Simulation* current = nullptr;

	private:
		static constexpr int NONE = -1;
		static constexpr int TANGENTS = 2;

		struct Body
		{
			Rigidbody* rigidbody;
			GameObject* object;

			glm::vec3 position;
			glm::quat orientation;

			// Held here while the solver works, and written back after.
			glm::vec3 velocity;
			glm::vec3 angularVelocity;

			// What the solver moves it by: none while it sleeps.
			float inverseMass;
			glm::mat3 inverseInertia;

			// What it is moved by once awake.
			float baseInverseMass;
			glm::mat3 baseInverseInertia;

			bool awake;

			std::vector<Collider*> colliders;
		};

		struct Contact
		{
			glm::vec3 point;
			glm::vec3 normal;

			// Below zero, the gap still between them.
			float depth;

			glm::vec3 fromA, fromB;
			glm::vec3 tangents[TANGENTS];

			float normalMass;
			float tangentMass[TANGENTS];
			float bias;

			float normalImpulse;
			float tangentImpulse[TANGENTS];
		};

		// Ordered by address, so each pair has one key.
		typedef std::pair<const Collider*, const Collider*> Key;

		struct KeyHash
		{
			// An odd multiplier, so the two halves do not cancel.
			static constexpr size_t MIX = 31;

			size_t operator()(const Key& key) const
			{
				return std::hash<const void*>()(key.first) * MIX ^ std::hash<const void*>()(key.second);
			};
		};

		struct Pair
		{
			Collider* a;
			Collider* b;

			// Indices into m_bodies, or NONE for a static collider.
			int bodyA;
			int bodyB;

			bool trigger;
			float friction;
			float bounciness;

			// Into m_contacts.
			size_t first;
			size_t count;

			// Whether any contact actually overlaps, rather than only nearing.
			bool touching;
		};

		// What a pair ended the last step it touched with.
		struct Touching
		{
			bool trigger;
			std::vector<Contact> contacts;

			// The step it was last seen touching in.
			unsigned step;
		};

		// Fills in a body Add only noted, false for one that cannot move.
		bool Ready(Body& body);

		void Gather(float seconds);
		void Prepare(Pair& pair, float seconds);
		void Solve(Pair& pair);

		// Gives a's side of the contact -impulse and b's +impulse.
		void Push(const Pair& pair, const Contact& contact, const glm::vec3& impulse);
		void Wake(int body);

		// The Rigidbody the collider moves with, if it has one, at once and
		// from outside a step too.
		static void WakeOwner(const Collider& collider);
		void Integrate(Body& body, float seconds);
		void Report();

		int BodyOf(const Collider& collider) const;
		glm::vec3 VelocityAt(int body, const glm::vec3& point) const;

		ColliderTree& m_tree;

		std::vector<Body> m_bodies{ };

		// This step's, reused from step to step.
		std::vector<Pair> m_pairs{ };
		std::vector<Contact> m_contacts{ };
		std::unordered_set<Key, KeyHash> m_found{ };
		std::vector<Contacts::Manifold> m_manifolds{ };

		std::unordered_map<Key, Touching, KeyHash> m_touching{ };
		unsigned m_step = 0;
	};
};
