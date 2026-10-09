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

			bool dynamic;
			bool awake;

			// Moved by Integrate, its leaves not yet placed.
			bool moved;

			// How far it goes this step.
			float travel;

			std::vector<Collider*> colliders;
		};

		// The normal, then two tangents.
		static constexpr int DIRECTIONS = 3;
		static constexpr int NORMAL = 0;

		// One direction a contact pushes along, and what a push along it does
		// to each side.
		struct Axis
		{
			glm::vec3 direction;

			// From the centre of mass to the contact, crossed with direction;
			// and that, through the inverse inertia.
			glm::vec3 angularA, angularB;
			glm::vec3 turnA, turnB;

			// The impulse it takes to change the speed along it by one.
			float mass;
			float impulse;
		};

		struct Contact
		{
			glm::vec3 point;
			glm::vec3 normal;

			// Below zero, the gap still between them.
			float depth;

			// The speed along the normal the solver aims for.
			float bias;

			Axis axes[DIRECTIONS];
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

		// Its pose, mass and inertia, for one that is awake; false for one
		// scaled to nothing.
		bool Weigh(Body& body);

		void Gather(float seconds);

		// What one chunk of candidates gave: its pairs, their contacts, and
		// the sleepers they wake.
		struct Considered
		{
			std::vector<Pair> pairs;
			std::vector<Contact> contacts;
			std::vector<int> wakes;

			// Reused from pair to pair.
			std::vector<Contacts::Manifold> manifolds;
		};

		// Collides a candidate pair, adding it to out if they touch or soon
		// will. Reads only, so chunks of candidates run at once.
		void Consider(Collider& a, Collider& b, Considered& out) const;

		static Key Ordered(const Collider* a, const Collider* b);

		bool Awake(int body) const;
		void Prepare(Pair& pair, float seconds);
		void Solve(Pair& pair);

		// How fast b moves away from a along axis, at its contact.
		static float Speed(const Body& a, const Body& b, const Axis& axis);

		// Gives a -impulse along axis and b +impulse.
		static void Push(Body& a, Body& b, const Axis& axis, float impulse);
		void Wake(int body);

		// The Rigidbody the collider moves with, if it has one, at once and
		// from outside a step too.
		static void WakeOwner(const Collider& collider);

		// Moves an awake body, writes its transform and measures its
		// colliders, touching nothing but the body, so many move at once.
		void Integrate(Body& body, float seconds);

		// Places a moved body's leaves in the tree.
		void Place(Body& body, float seconds);

		static int Depth(const GameObject& object);
		void Report();

		int BodyOf(const Collider& collider) const;

		// The body, or for NONE one that never moves.
		Body& At(int body);
		glm::vec3 VelocityAt(int body, const glm::vec3& point);

		ColliderTree& m_tree;

		std::vector<Body> m_bodies{ };

		// Stands in for a static collider: no mass, no velocity.
		Body m_ground{ };

		// This step's bodies with others hanging under them.
		std::vector<Body*> m_parents{ };

		// This step's, reused from step to step.
		std::vector<Pair> m_pairs{ };
		std::vector<Contact> m_contacts{ };
		// Each step's, by chunk, reused from step to step.
		std::vector<std::vector<Key>> m_found{ };
		std::vector<Key> m_live{ };
		std::vector<Considered> m_considered{ };

		std::unordered_map<Key, Touching, KeyHash> m_touching{ };

		// Pairs whose leaves overlap, kept from step to step in a vector, so
		// the solver meets them in the same order every run.
		std::vector<Key> m_candidates{ };
		std::unordered_set<Key, KeyHash> m_candidateSet{ };
		unsigned m_step = 0;
	};
};
