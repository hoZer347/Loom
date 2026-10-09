#pragma once

#include "Collision.h"
#include "Contacts.h"
#include "Jobs.h"

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
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

		// A collider the walk has found, from its OnPhysics. The walk calls a
		// GameObject's physics components one after another, so a body's
		// colliders are those noted beside it.
		void Note(Collider& collider) { m_noted.push_back(&collider); };

		void Step(float seconds);

		// A collider leaving its tree, so no pair outlives it.
		void Forget(const Collider& collider);

		// The simulation of the scene being stepped.
		static inline Simulation* current = nullptr;

	private:
		static constexpr int NONE = -1;

		// Bodies and contacts each start a line of their own, so threads
		// solving neighbours in memory do not slow one another.
		static constexpr size_t CACHE_LINE = 64;

		struct alignas(CACHE_LINE) Body
		{
			Rigidbody* rigidbody;
			GameObject* object;

			glm::vec3 position;
			glm::quat orientation;

			// Held here while the solver works, and written back after.
			glm::vec3 velocity;
			glm::vec3 angularVelocity;

			// The same, as the solver works on them: a fourth lane of zero
			// each, so it can take all three at once.
			glm::vec4 linear;
			glm::vec4 angular;

			// What the solver moves it by: none while it sleeps.
			float inverseMass;
			glm::mat3 inverseInertia;

			bool dynamic;
			bool awake;

			// Its leaves no longer cover it, and are yet to be refitted.
			bool refit;

			// How far it goes this step.
			float travel;

			// How many contacts it has, and the slab the solver puts it in.
			size_t load;
			int slab;

			// Those on its GameObject, among the step's noted ones.
			std::span<Collider* const> colliders;
		};

		// A body Add has noted, and how many colliders were noted before it.
		struct Joining
		{
			Rigidbody* rigidbody;
			size_t noted;
		};

		// The normal, then two tangents.
		static constexpr int DIRECTIONS = 3;
		static constexpr int NORMAL = 0;

		// One direction a contact pushes along, and what a push along it does
		// to each side: each with a fourth lane of zero, as the solver takes
		// them.
		struct Axis
		{
			glm::vec4 direction;

			// From the centre of mass to the contact, crossed with direction;
			// and that, through the inverse inertia.
			glm::vec4 angularA, angularB;
			glm::vec4 turnA, turnB;

			// The impulse it takes to change the speed along it by one.
			float mass;
			float impulse;
		};

		struct alignas(CACHE_LINE) Contact
		{
			glm::vec3 point;
			glm::vec3 normal;

			// Below zero, the gap still between them.
			float depth;

			// The speed along the normal the solver aims for.
			float bias;

			Axis axes[DIRECTIONS];
		};

		// Ordered by when each was made, so each pair has one key.
		typedef std::pair<const Collider*, const Collider*> Key;

		// A set of keys in one array, open addressed, which the thousands of
		// lookups a step makes go through far faster than a node each.
		struct KeySet
		{
			// False for one already in.
			bool Insert(const Key& key);
			void Erase(const Key& key);

			template <typename Doomed>
			void EraseIf(const Doomed& doomed)
			{
				std::vector<Key> kept{ };

				for (const Key& key : m_slots)
					if (key.first != nullptr && !doomed(key))
						kept.push_back(key);

				std::fill(m_slots.begin(), m_slots.end(), Key{ });
				m_count = 0;

				for (const Key& key : kept)
					Insert(key);
			};

		private:
			// Where key would sit with nothing in its way.
			size_t Home(const Key& key) const;
			void Grow();

			// Empty ones hold nulls.
			std::vector<Key> m_slots{ };
			size_t m_count = 0;
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

			// In its chunk's contacts, once the chunks are put together.
			Contact* contacts;
			size_t count;

			// Into m_candidates.
			size_t candidate;

			// Whether any contact actually overlaps, rather than only nearing,
			// and whether it is the first step it has.
			bool touching;
			bool entering;

			// Whether each side has a component told of contacts.
			bool aListens;
			bool bListens;
		};

		// Where a contact was, and the impulses it ended a step on, to warm
		// start the same contact with the next step.
		struct Kept
		{
			glm::vec3 point;
			glm::vec3 normal;
			float impulses[DIRECTIONS];
		};

		// A pair whose leaves overlap, kept from step to step, with what it
		// ended the last step it touched with.
		struct Candidate
		{
			Key key;

			// Whether it touched as of the last report, and as a trigger.
			bool touching;
			bool trigger;

			// The step it was last seen touching in.
			unsigned step;

			// Whether each side could be pushed then. Impulses kept against
			// one that could not, asleep, are no start for one that now can.
			bool movableA;
			bool movableB;

			std::vector<Kept> kept;
		};

		// Fills in a body Add only noted, false for one that cannot move.
		bool Ready(Body& body, const Joining& joining);

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
		void Consider(size_t candidate, Considered& out) const;

		static Key Ordered(const Collider* a, const Collider* b);

		bool Awake(int body) const;

		// How far the body goes this step; none for a static collider.
		float Travel(int body) const;

		// Whether any of the body's leaves no longer covers its travel.
		bool Outgrown(const Body& body, float seconds) const;

		// What becomes of a candidate this step.
		enum class Fate : char
		{
			// Kept, with nothing to look at: neither side is awake, so nothing
			// has moved to part them, or their own boxes are too far apart to
			// touch this step.
			Still,
			// Their leaves no longer overlap.
			Parted,
			Live,
		};

		// Gravity and what AddForce gathered, into the body's velocity.
		static void Accelerate(Body& body, const glm::vec3& gravity, float seconds);

		// Makes a pair's contacts ready to solve, from the velocities as they
		// stand; reads the bodies and writes only the pair's own contacts, so
		// many pairs are made ready at once.
		void Prepare(Pair& pair, float seconds);

		// Splits the pairs to solve between slabs of space, the borders
		// between them, and those across more than one border: no two slabs
		// share a body the solver moves, nor two borders but neighbours.
		void Partition();

		// Warm starts and solves the slabs' pairs, each slab's on one thread.
		void SolveSlabs();

		// The body, for one the solver moves.
		Body* Solved(int body);

		// Gives a pair's bodies the impulses its contacts kept from last step.
		void WarmStart(Pair& pair);
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

		// Refits the leaves of a body that has outgrown them.
		void Place(Body& body, float seconds);

		static int Depth(const GameObject& object);

		// Keeps, with each touching pair's candidate, what it ended this step
		// on; for pairs [begin, end), each its own, so many are kept at once.
		void Keep(size_t begin, size_t end);

		// Tells listeners of the pairs touching, and of those that no longer
		// do, once Keep has kept them all.
		void Report();

		int BodyOf(const Collider& collider) const;

		// The body, or for NONE one that never moves.
		Body& At(int body);
		glm::vec3 VelocityAt(int body, const glm::vec3& point);

		ColliderTree& m_tree;

		std::vector<Body> m_bodies{ };

		// This step's bodies and colliders, in the order the walk found them.
		std::vector<Joining> m_joining{ };
		std::vector<Collider*> m_noted{ };

		// Stands in for a static collider: no mass, no velocity.
		Body m_ground{ };

		// This step's bodies with others hanging under them.
		std::vector<Body*> m_parents{ };

		// This step's, reused from step to step; their contacts are kept by
		// the chunks of m_considered that found them.
		std::vector<Pair> m_pairs{ };

		// This step's pair indices by slab, then by the border after each
		// slab but the last, then those solved alone, then those only made
		// ready; and how many slabs.
		std::vector<std::vector<size_t>> m_groups{ };
		size_t m_slabs = 0;

		// Where the slabs are cut, and the load along their axis.
		std::vector<float> m_cuts{ };
		std::vector<size_t> m_histogram{ };

		// Each step's, by chunk, reused from step to step.
		std::vector<std::vector<Key>> m_found{ };
		// Into m_candidates.
		std::vector<size_t> m_live{ };
		std::vector<Fate> m_fates{ };
		std::vector<Considered> m_considered{ };

		// Kept in a vector, so the solver meets them in the same order every
		// run; with a set of their keys, so none is found twice.
		std::vector<Candidate> m_candidates{ };
		KeySet m_candidateSet{ };

		// This step's candidates whose leaves parted while they touched.
		std::vector<Candidate> m_parted{ };

		unsigned m_step = 0;
	};
};
