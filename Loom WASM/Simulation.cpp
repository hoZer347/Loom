#include "Simulation.h"

#include "Collider.h"
#include "ColliderTree.h"
#include "GameObject.h"
#include "Jobs.h"
#include "Physics.h"
#include "Profiler.h"
#include "Rigidbody.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/euler_angles.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>

#if defined(_M_X64) || defined(__SSE2__)
#include <immintrin.h>
#endif


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

		// How many leaves one job's chunk queries, how many bodies it readies
		// or moves, how many candidates it collides, how many pairs it
		// reports and how many candidates it checks for parting: enough that a
		// chunk outweighs handing it out.
		constexpr size_t QUERY_GRAIN = 16;
		constexpr size_t BODY_GRAIN = 64;
		constexpr size_t CONSIDER_GRAIN = 128;
		constexpr size_t PAIR_GRAIN = 64;
		constexpr size_t PRUNE_GRAIN = 512;

		// The solver splits the bodies in pairs into a slab for each of the
		// machine's cores, but no fewer bodies to a slab than this, so each is
		// worth handing out; cut where a histogram of this many bins along the
		// slabs' axis has them share the contacts evenly.
		constexpr size_t SLAB_BODIES = 64;
		constexpr size_t BINS = 256;

		// A contact this close to one from the last step, facing nearly the
		// same way, is taken to be the same one.
		constexpr float SAME_DISTANCE = 0.05f;
		constexpr float SAME_FACING = 0.95f;

		// Of a uniform box about its centre, by its half extents; of a solid
		// sphere, by its radius.
		constexpr float BOX_INERTIA = 1.0f / 3.0f;
		constexpr float SPHERE_INERTIA = 2.0f / 5.0f;
		constexpr float HALF = 0.5f;

#if defined(_M_X64) || defined(__SSE2__)
		// The four lanes of a vec4 at once.
		struct Wide
		{
			__m128 lanes;
		};

		// The shuffle that brings a vector's second lane to its first.
		constexpr int SECOND_LANE = 1;

		Wide Load(const glm::vec4& v) { return { _mm_loadu_ps(&v.x) }; };
		void Store(glm::vec4& v, Wide wide) { _mm_storeu_ps(&v.x, wide.lanes); };

		Wide operator+(Wide a, Wide b) { return { _mm_add_ps(a.lanes, b.lanes) }; };
		Wide operator-(Wide a, Wide b) { return { _mm_sub_ps(a.lanes, b.lanes) }; };
		Wide operator*(Wide a, Wide b) { return { _mm_mul_ps(a.lanes, b.lanes) }; };
		Wide operator*(Wide a, float scale) { return { _mm_mul_ps(a.lanes, _mm_set1_ps(scale)) }; };

		float Sum(Wide wide)
		{
			const __m128 halves = _mm_add_ps(wide.lanes, _mm_movehl_ps(wide.lanes, wide.lanes));
			return _mm_cvtss_f32(_mm_add_ss(halves, _mm_shuffle_ps(halves, halves, SECOND_LANE)));
		};
#else
		typedef glm::vec4 Wide;

		Wide Load(const glm::vec4& v) { return v; };
		void Store(glm::vec4& v, Wide wide) { v = wide; };

		float Sum(Wide wide) { return wide.x + wide.y + wide.z + wide.w; };
#endif

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
		m_joining.push_back({ &rigidbody, m_noted.size() });
	};

	bool Simulation::Ready(Body& body, const Joining& joining)
	{
		Rigidbody& rigidbody = *joining.rigidbody;

		body = Body{ };
		body.rigidbody = &rigidbody;
		body.object = rigidbody.m_gameObject;

		// Those on its GameObject, on either side of where it was added,
		// all in the tree by now.
		size_t first = joining.noted;
		size_t end = joining.noted;

		while (first > 0 && m_noted[first - 1]->m_gameObject == body.object)
			first--;

		while (end < m_noted.size() && m_noted[end]->m_gameObject == body.object)
			end++;

		body.colliders = std::span(m_noted).subspan(first, end - first);

		body.dynamic = !rigidbody.isKinematic && rigidbody.mass > 0.0f;

		body.placed = *body.object->transform.position;
		body.turned = *body.object->transform.rotation;

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

		if (world != rigidbody.m_posedAt)
		{
			glm::mat3 rotation(1.0f);

			for (int axis = 0; axis < glm::vec3::length(); axis++)
			{
				rigidbody.m_posedScale[axis] = glm::length(glm::vec3(world[axis]));

				if (rigidbody.m_posedScale[axis] == 0.0f)
					return false;

				rotation[axis] = glm::vec3(world[axis]) / rigidbody.m_posedScale[axis];
			};

			rigidbody.m_posedAt = world;
			rigidbody.m_posedOrientation = glm::normalize(glm::quat_cast(rotation));
		};

		body.orientation = rigidbody.m_posedOrientation;
		const glm::vec3 scale = rigidbody.m_posedScale;

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

		// Turned into the world: each own axis's inverse inertia along it.
		const glm::mat3 turned = glm::mat3_cast(body.orientation);
		body.inverseInertia = glm::mat3(0.0f);

		for (int axis = 0; axis < glm::vec3::length(); axis++)
			body.inverseInertia += glm::outerProduct(turned[axis], turned[axis]) * rigidbody.m_inverseInertia[axis];
		body.inverseMass = body.dynamic ? 1.0f / rigidbody.mass : 0.0f;

		return true;
	};

#ifndef __EMSCRIPTEN__
	struct Simulation::Solver
	{
		std::mutex mutex{ };
		std::condition_variable handed{ };
		std::condition_variable solved{ };

		// Each taken off once solved, so one waiting knows its step is done
		// when it is gone.
		std::deque<Simulation*> queue{ };
		bool stopping = false;

		// Last, so it starts once the rest is ready.
		std::thread thread{ [this]() { Serve(); } };

		~Solver()
		{
			{
				const std::lock_guard lock(mutex);
				stopping = true;
			};

			handed.notify_one();
			thread.join();
		};

		void Serve()
		{
			// After Main, being the first to name itself with that order.
			Profiler::NameThread("Physics", 0);

			std::unique_lock lock(mutex);

			while (true)
			{
				handed.wait(lock, [this]() { return stopping || !queue.empty(); });

				if (stopping)
					return;

				Simulation& simulation = *queue.front();

				lock.unlock();
				simulation.Resolve(simulation.m_seconds);
				lock.lock();

				queue.pop_front();
				solved.notify_all();
			};
		};
	};

	Simulation::Solver& Simulation::Solving()
	{
		static Solver solver;
		return solver;
	};
#endif

	void Simulation::Await()
	{
#ifndef __EMSCRIPTEN__
		Solver& solver = Solving();
		std::unique_lock lock(solver.mutex);

		solver.solved.wait(lock, [&]() { return std::ranges::find(solver.queue, this) == solver.queue.end(); });
#endif
	};

	Simulation::~Simulation()
	{
		Drop();
	};

	void Simulation::Note(Collider& collider)
	{
		collider.m_listens = Listens(*collider.m_gameObject);
		m_noted.push_back(&collider);
	};

	void Simulation::Step(float seconds)
	{
		LOOM_STAGE("Simulation::Step");

		End();
		m_step++;

		{
			LOOM_STAGE("Simulation::ReadyBodies");
			ReadyBodies(seconds);
		};

		Resolve(seconds);

		{
			LOOM_STAGE("Simulation::MoveBodies");
			MoveBodies(seconds);
		};

		{
			LOOM_STAGE("Simulation::Report");
			Report();
		};

		Clear();
	};

	void Simulation::Begin(float seconds)
	{
		LOOM_STAGE("Simulation::Begin");

		End();
		m_step++;

		{
			LOOM_STAGE("Simulation::ReadyBodies");
			ReadyBodies(seconds);
		};

		m_seconds = seconds;
		m_begun = true;
		m_landed = false;
		s_begun.push_back(this);

#ifdef __EMSCRIPTEN__
		Resolve(seconds);
#else
		Solver& solver = Solving();

		{
			const std::lock_guard lock(solver.mutex);
			solver.queue.push_back(this);
		};

		solver.handed.notify_one();
#endif
	};

	void Simulation::Land()
	{
		if (!m_begun || m_landed)
			return;

		LOOM_STAGE("Simulation::Land");

		Await();
		m_landed = true;

		LOOM_STAGE("Simulation::MoveBodies");
		MoveBodies(m_seconds);
	};

	void Simulation::End()
	{
		if (!m_begun)
			return;

		LOOM_STAGE("Simulation::End");

		Land();

		// Taken off first, so a listener's change to the tree lands nothing.
		m_begun = false;
		std::erase(s_begun, this);

		{
			LOOM_STAGE("Simulation::Report");
			Report();
		};

		Clear();
	};

	void Simulation::EndAll()
	{
		while (!s_begun.empty())
			s_begun.front()->End();
	};

	void Simulation::Drop()
	{
		if (!m_begun)
			return;

		Await();

		m_begun = false;
		std::erase(s_begun, this);

		Clear();
	};

	void Simulation::Resolve(float seconds)
	{
		LOOM_STAGE("Simulation::Resolve");

		{
			LOOM_STAGE("Simulation::Gather");
			Gather();
		};

		{
			LOOM_STAGE("Simulation::Partition");
			Partition();
		};

		{
			LOOM_STAGE("Simulation::PrepareContacts");
			PrepareContacts(seconds);
		};

		{
			LOOM_STAGE("Simulation::SolveSlabs");
			SolveSlabs();
		};
	};

	void Simulation::Clear()
	{
		for (const Body& body : m_bodies)
			for (Collider* collider : body.colliders)
				collider->m_body = NONE;

		m_joining.clear();
		m_noted.clear();
	};

	void Simulation::ReadyBodies(float seconds)
	{
		// Once the tree is in step, so a collider attached since the body
		// was is in it and moves with the body. The bodies are kept from step
		// to step, so readying one only overwrites it.
		m_bodies.resize(m_joining.size());

		// Each readied, then gravity and what AddForce gathered taken into
		// its velocity, and how far that takes it found.
		const glm::vec3 gravity = Physics::GetGravity();

		Jobs::For(m_bodies.size(), BODY_GRAIN,
			[&](size_t, size_t begin, size_t end)
			{
				for (size_t i = begin; i < end; i++)
				{
					Body& body = m_bodies[i];

					if (!Ready(body, m_joining[i]))
					{
						body.object = nullptr;
						continue;
					};

					for (Collider* collider : body.colliders)
						collider->m_body = (int)i;

					Accelerate(body, gravity, seconds);

					body.travel = glm::length(body.velocity) * seconds;
					body.refit = body.awake && Outgrown(body, seconds);
				};
			});

		// Numbered again, past any that cannot move.
		if (std::erase_if(m_bodies, [](const Body& body) { return body.object == nullptr; }) > 0)
			for (int index = 0; index < (int)m_bodies.size(); index++)
				for (Collider* collider : m_bodies[index].colliders)
					collider->m_body = index;

		// Each moving body's leaf reaches along the travel it is about to
		// make, which a velocity set or changed since the last step may not:
		// those found to have outgrown theirs are refitted. The last change to
		// the tree, so the contacts are found and solved in a tree held still.
		for (Body& body : m_bodies)
			if (body.refit)
			{
				for (const Collider* collider : body.colliders)
					m_tree.Reach(*collider, body.velocity * seconds);

				body.refit = false;
			};

		m_moved = m_tree.TakeMoved();
	};

	void Simulation::PrepareContacts(float seconds)
	{
		// Every pair's contacts made ready at once, from the velocities before
		// any warm start.
		Jobs::For(m_groups.size(), 1,
			[&](size_t, size_t begin, size_t end)
			{
				for (size_t group = begin; group < end; group++)
					for (size_t pair : m_groups[group])
						Prepare(m_pairs[pair], seconds);
			});
	};

	void Simulation::MoveBodies(float seconds)
	{
		// A body with others hanging under it moves first, outermost first,
		// so those under it are placed from where it ends up; the rest move at
		// once, then their leaves are placed.
		m_parents.clear();

		for (Body& body : m_bodies)
		{
			Rigidbody& rigidbody = *body.rigidbody;
			const glm::vec3 linear(body.linear);
			const glm::vec3 angular(body.angular);

			if (body.awake && rigidbody.m_sleeping)
				rigidbody.WakeUp();

			// Unless set since the step was readied, which wins.
			if (rigidbody.velocity == body.velocity)
				rigidbody.velocity = linear;

			if (rigidbody.angularVelocity == body.angularVelocity)
				rigidbody.angularVelocity = angular;

			body.velocity = linear;
			body.angularVelocity = angular;

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

		// The moved leaves placed in the tree as one piece of work, the first
		// handed out, while the rest keep what each touching pair ended on.
		const size_t keeping = Jobs::Chunks(m_pairs.size(), PAIR_GRAIN);

		Jobs::For(1 + keeping, 1,
			[&](size_t chunk, size_t, size_t)
			{
				if (chunk == 0)
				{
					for (Body& body : m_bodies)
						if (body.object->GetChildren().empty())
							Place(body, seconds);

					return;
				};

				const size_t begin = (chunk - 1) * PAIR_GRAIN;
				Keep(begin, std::min(begin + PAIR_GRAIN, m_pairs.size()));
			});
	};

	Simulation::Key Simulation::Ordered(const Collider* a, const Collider* b)
	{
		return a->m_made < b->m_made ? Key{ a, b } : Key{ b, a };
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

		body.linear = glm::vec4(body.velocity, 0.0f);
		body.angular = glm::vec4(body.angularVelocity, 0.0f);
	};

	void Simulation::Forget(const Collider& collider)
	{
		// Only one destroyed comes while a step is solved, which the rules
		// forbid; that step goes rather than move a collider half gone.
		if (!m_landed)
			Drop();

		const auto involves =
			[&](const Key& key) { return key.first == &collider || key.second == &collider; };

		m_candidateSet.EraseIf(involves);

		std::erase_if(m_candidates,
			[&](const Candidate& candidate)
			{
				const bool first = candidate.key.first == &collider;

				if (!first && candidate.key.second != &collider)
					return false;

				// What it held up has to be free to fall.
				if (candidate.touching)
					WakeOwner(*(first ? candidate.key.second : candidate.key.first));

				return true;
			});

		std::erase_if(m_parted, [&](const Candidate& candidate) { return involves(candidate.key); });
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

		// Its Rigidbody is woken as it moves, back on the caller's thread;
		// Weigh only refreshes the pose and inertia kept on it for the
		// simulation.
		if (Weigh(sleeper))
			sleeper.awake = true;
	};

	void Simulation::Gather()
	{
		m_pairs.clear();

		// New candidates: each leaf put in or refitted since the last step,
		// against whatever it now overlaps, found in parallel and taken in
		// chunk order. Leaves cover their bodies' travel, so two that can
		// meet this step overlap.
		m_found.resize(std::max(m_found.size(), Jobs::Chunks(m_moved.size(), QUERY_GRAIN)));

		Jobs::For(m_moved.size(), QUERY_GRAIN,
			[&](size_t chunk, size_t begin, size_t end)
			{
				std::vector<Key>& found = m_found[chunk];
				found.clear();

				for (size_t i = begin; i < end; i++)
				{
					const Collider* collider = m_moved[i];
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

		for (size_t chunk = 0; chunk < Jobs::Chunks(m_moved.size(), QUERY_GRAIN); chunk++)
			for (const Key& key : m_found[chunk])
				if (m_candidateSet.Insert(key))
					m_candidates.push_back({ key });

		// Candidates whose leaves have parted are dropped, and the rest to be
		// looked at gathered. Where neither side is awake, nothing has moved
		// to part them.
		m_fates.resize(m_candidates.size());

		Jobs::For(m_candidates.size(), PRUNE_GRAIN,
			[&](size_t, size_t begin, size_t end)
			{
				for (size_t i = begin; i < end; i++)
				{
					const Key& key = m_candidates[i].key;

					if (!Awake(BodyOf(*key.first)) && !Awake(BodyOf(*key.second)))
					{
						m_fates[i] = Fate::Still;
						continue;
					};

					const Bounds* first = m_tree.PaddedBounds(*key.first);
					const Bounds* second = m_tree.PaddedBounds(*key.second);

					if (first == nullptr || second == nullptr || !first->Overlaps(*second))
					{
						m_fates[i] = Fate::Parted;
						continue;
					};

					// Their own boxes, as far apart as Consider would find them
					// without a trigger, which only brings them closer.
					const float margin = Travel(BodyOf(*key.first)) + Travel(BodyOf(*key.second)) + SPECULATIVE_SLACK;

					m_fates[i] = key.first->m_bounds.Expanded(margin).Overlaps(key.second->m_bounds) ? Fate::Live : Fate::Still;
				};
			});

		// From the back, each parted one swapped for the last, which has
		// already been seen.
		for (size_t i = m_candidates.size(); i-- > 0; )
		{
			if (m_fates[i] != Fate::Parted)
				continue;

			Candidate& candidate = m_candidates[i];
			m_candidateSet.Erase(candidate.key);

			if (candidate.touching)
				m_parted.push_back(std::move(candidate));

			if (i + 1 < m_candidates.size())
			{
				candidate = std::move(m_candidates.back());
				m_fates[i] = m_fates.back();
			};

			m_candidates.pop_back();
			m_fates.pop_back();
		};

		m_live.clear();

		for (size_t i = 0; i < m_candidates.size(); i++)
			if (m_fates[i] == Fate::Live)
				m_live.push_back(i);

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
					Consider(m_live[i], out);
			});

		for (size_t chunk = 0; chunk < chunks; chunk++)
		{
			Considered& out = m_considered[chunk];
			Contact* contacts = out.contacts.data();

			// Each pair's contacts follow the last's.
			for (Pair pair : out.pairs)
			{
				pair.contacts = contacts;
				contacts += pair.count;
				m_pairs.push_back(pair);
			};

			for (int sleeper : out.wakes)
				if (!m_bodies[sleeper].awake)
					Wake(sleeper);
		};
	};

	float Simulation::Travel(int body) const
	{
		return body == NONE ? 0.0f : m_bodies[body].travel;
	};

	bool Simulation::Awake(int body) const
	{
		return body != NONE && m_bodies[body].awake;
	};

	void Simulation::Consider(size_t candidate, Considered& out) const
	{
		Collider& a = *const_cast<Collider*>(m_candidates[candidate].key.first);
		Collider& b = *const_cast<Collider*>(m_candidates[candidate].key.second);

		const auto dynamic =
			[&](int body) { return body != NONE && m_bodies[body].dynamic; };

		Pair pair{ };
		pair.a = &a;
		pair.b = &b;
		pair.candidate = candidate;
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

		const float margin = pair.trigger ? 0.0f : Travel(pair.bodyA) + Travel(pair.bodyB) + SPECULATIVE_SLACK;

		// Their own boxes still too far apart to touch this step.
		if (!a.m_bounds.Expanded(margin).Overlaps(b.m_bounds))
			return;

		out.manifolds.clear();
		Contacts::Collide(a.m_shape, b.m_shape, margin, out.manifolds);

		const size_t start = out.contacts.size();

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

		pair.count = out.contacts.size() - start;

		if (pair.count == 0 || (pair.trigger && !pair.touching))
		{
			out.contacts.resize(start);
			return;
		};

		pair.aListens = a.m_listens;
		pair.bListens = b.m_listens;

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

		const Candidate& candidate = m_candidates[pair.candidate];
		const bool movesA = a.inverseMass > 0.0f;
		const bool movesB = b.inverseMass > 0.0f;
		const bool spinsA = movesA || a.angularVelocity != glm::vec3(0.0f);
		const bool spinsB = movesB || b.angularVelocity != glm::vec3(0.0f);
		const bool warm = candidate.touching && candidate.movableA == movesA && candidate.movableB == movesB;

		for (size_t i = 0; i < pair.count; i++)
		{
			Contact& contact = pair.contacts[i];

			const glm::vec3 fromA = contact.point - a.position;
			const glm::vec3 fromB = contact.point - b.position;

			const glm::vec3 tangent = Perpendicular(contact.normal);
			const glm::vec3 directions[DIRECTIONS] = { contact.normal, tangent, glm::cross(contact.normal, tangent) };

			// What a push along each direction does to each side, worked out
			// once here so the iterations need only dot products.
			for (int d = 0; d < DIRECTIONS; d++)
			{
				Axis& axis = contact.axes[d];

				// Nothing turns a side the solver cannot move, and one not
				// spinning adds nothing to the speed.
				const glm::vec3 angularA = spinsA ? glm::cross(fromA, directions[d]) : glm::vec3(0.0f);
				const glm::vec3 angularB = spinsB ? glm::cross(fromB, directions[d]) : glm::vec3(0.0f);
				const glm::vec3 turnA = movesA ? a.inverseInertia * angularA : glm::vec3(0.0f);
				const glm::vec3 turnB = movesB ? b.inverseInertia * angularB : glm::vec3(0.0f);

				axis.direction = glm::vec4(directions[d], 0.0f);
				axis.angularA = glm::vec4(angularA, 0.0f);
				axis.angularB = glm::vec4(angularB, 0.0f);
				axis.turnA = glm::vec4(turnA, 0.0f);
				axis.turnB = glm::vec4(turnB, 0.0f);

				const float inverse = a.inverseMass + b.inverseMass
					+ glm::dot(angularA, turnA) + glm::dot(angularB, turnB);

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

			if (warm)
				for (const Kept& old : candidate.kept)
				{
					const glm::vec3 apart = old.point - contact.point;

					if (glm::dot(apart, apart) < SAME_DISTANCE * SAME_DISTANCE && glm::dot(old.normal, contact.normal) > SAME_FACING)
					{
						// The tangents are the same two, the normal being so close.
						for (int d = 0; d < DIRECTIONS; d++)
							contact.axes[d].impulse = old.impulses[d];

						break;
					};
				};

		};
	};

	void Simulation::Partition()
	{
		// How many contacts each body has, which is what the slabs share out,
		// and how far apart those in any lie along each axis.
		Bounds spread{ glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest()) };
		size_t loaded = 0;
		size_t load = 0;

		for (const Pair& pair : m_pairs)
		{
			if (pair.trigger)
				continue;

			for (Body* body : { Solved(pair.bodyA), Solved(pair.bodyB) })
				if (body)
				{
					loaded += body->load == 0;
					body->load += pair.count;
					load += pair.count;

					spread.min = glm::min(spread.min, body->position);
					spread.max = glm::max(spread.max, body->position);
				};
		};

		const size_t slabs = std::clamp<size_t>(loaded / SLAB_BODIES, 1, Jobs::Cores());

		// Cut along the widest axis where an equal share of the load lies
		// either side, from a histogram of it.
		const glm::vec3 extent = spread.max - spread.min;
		const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : extent.y >= extent.z ? 1 : 2;
		const float low = spread.min[axis];
		const float width = extent[axis] / BINS;

		m_cuts.assign(slabs - 1, std::numeric_limits<float>::max());

		if (slabs > 1 && width > 0.0f)
		{
			m_histogram.assign(BINS, 0);

			for (const Body& body : m_bodies)
				if (body.load > 0)
					m_histogram[std::min<size_t>((size_t)((body.position[axis] - low) / width), BINS - 1)] += body.load;

			size_t below = 0;
			size_t cut = 0;

			for (size_t bin = 0; bin < BINS && cut < m_cuts.size(); bin++)
			{
				below += m_histogram[bin];

				while (cut < m_cuts.size() && below * slabs >= load * (cut + 1))
					m_cuts[cut++] = low + width * (bin + 1);
			};
		};

		for (Body& body : m_bodies)
			body.slab = (int)(std::upper_bound(m_cuts.begin(), m_cuts.end(), body.position[axis]) - m_cuts.begin());

		// A pair inside one slab is that slab's to solve; one across two
		// side by side, the border's; any other, solved alone; and one with
		// nothing to move only made ready, for what it reports.
		const size_t borders = slabs - 1;
		const size_t across = slabs + borders;
		const size_t idle = across + 1;

		m_groups.resize(idle + 1);

		for (std::vector<size_t>& group : m_groups)
			group.clear();

		for (size_t i = 0; i < m_pairs.size(); i++)
		{
			const Pair& pair = m_pairs[i];

			const Body* a = Solved(pair.bodyA);
			const Body* b = Solved(pair.bodyB);

			if (pair.trigger)
				continue;

			if (a == nullptr && b == nullptr)
			{
				m_groups[idle].push_back(i);
				continue;
			};

			const int slabA = a ? a->slab : b->slab;
			const int slabB = b ? b->slab : a->slab;

			if (slabA == slabB)
				m_groups[slabA].push_back(i);
			else if (std::abs(slabA - slabB) == 1)
				m_groups[slabs + std::min(slabA, slabB)].push_back(i);
			else
				m_groups[across].push_back(i);
		};

		m_slabs = slabs;
		m_alone = across;
	};

	void Simulation::SolveSlabs()
	{
		const int slabs = (int)m_slabs;
		const bool lonely = !m_groups[m_alone].empty();

		// How many passes each slab, each border and those alone have been
		// through.
		const std::unique_ptr<std::atomic<int>[]> slabsDone(new std::atomic<int>[slabs]());
		const std::unique_ptr<std::atomic<int>[]> bordersDone(new std::atomic<int>[slabs]());
		std::atomic<int> aloneDone{ 0 };

		// The warm start, then each iteration.
		const auto run =
			[&](size_t group, int pass)
			{
				for (size_t pair : m_groups[group])
					if (pass == 0)
						WarmStart(m_pairs[pair]);
					else
						Solve(m_pairs[pair]);
			};

		const int team = (int)std::min<size_t>(Jobs::Threads(), m_slabs);

		// Each member keeps its slabs, and the border after each, through
		// every pass, waiting only on those it shares bodies with. Every
		// body meets its slab, its even border, its odd border, then those
		// alone, pass after pass: as one thread doing them all in that order
		// would, so the result is the same however many share them.
		Jobs::For(team, 1,
			[&](size_t member, size_t, size_t)
			{
				for (int pass = 0; pass <= ITERATIONS; pass++)
				{
					for (int k = (int)member; k < slabs; k += team)
					{
						if (k > 0)
							Jobs::WaitFor(bordersDone[k - 1], pass);

						if (lonely)
							Jobs::WaitFor(aloneDone, pass);

						run(k, pass);
						slabsDone[k].store(pass + 1, std::memory_order_release);
						slabsDone[k].notify_all();
					};

					for (int parity = 0; parity < 2; parity++)
						for (int k = (int)member; k < slabs - 1; k += team)
						{
							if (k % 2 != parity)
								continue;

							Jobs::WaitFor(slabsDone[k + 1], pass + 1);

							// An odd border follows the even ones either side.
							if (parity == 1)
							{
								Jobs::WaitFor(bordersDone[k - 1], pass + 1);

								if (k + 1 < slabs - 1)
									Jobs::WaitFor(bordersDone[k + 1], pass + 1);
							};

							run(m_slabs + k, pass);
							bordersDone[k].store(pass + 1, std::memory_order_release);
							bordersDone[k].notify_all();
						};

					if (lonely && member == 0)
					{
						for (int k = 0; k < slabs; k++)
							Jobs::WaitFor(slabsDone[k], pass + 1);

						for (int k = 0; k < slabs - 1; k++)
							Jobs::WaitFor(bordersDone[k], pass + 1);

						run(m_alone, pass);
						aloneDone.store(pass + 1, std::memory_order_release);
						aloneDone.notify_all();
					};
				};
			});
	};

	Simulation::Body* Simulation::Solved(int body)
	{
		return body != NONE && m_bodies[body].inverseMass > 0.0f ? &m_bodies[body] : nullptr;
	};

	void Simulation::WarmStart(Pair& pair)
	{
		if (pair.trigger)
			return;

		Body& a = At(pair.bodyA);
		Body& b = At(pair.bodyB);

		for (size_t i = 0; i < pair.count; i++)
			for (const Axis& axis : pair.contacts[i].axes)
				Push(a, b, axis, axis.impulse);
	};

	float Simulation::Speed(const Body& a, const Body& b, const Axis& axis)
	{
		return Sum((Load(b.linear) - Load(a.linear)) * Load(axis.direction)
			+ Load(b.angular) * Load(axis.angularB)
			- Load(a.angular) * Load(axis.angularA));
	};

	void Simulation::Push(Body& a, Body& b, const Axis& axis, float impulse)
	{
		// One that does not move is left untouched, as pairs in other slabs
		// may be pushing on it at once.
		if (a.inverseMass > 0.0f)
		{
			Store(a.linear, Load(a.linear) - Load(axis.direction) * (impulse * a.inverseMass));
			Store(a.angular, Load(a.angular) - Load(axis.turnA) * impulse);
		};

		if (b.inverseMass > 0.0f)
		{
			Store(b.linear, Load(b.linear) + Load(axis.direction) * (impulse * b.inverseMass));
			Store(b.angular, Load(b.angular) + Load(axis.turnB) * impulse);
		};
	};

	void Simulation::Solve(Pair& pair)
	{
		if (pair.trigger)
			return;

		Body& a = At(pair.bodyA);
		Body& b = At(pair.bodyB);

		for (size_t i = 0; i < pair.count; i++)
		{
			Contact& contact = pair.contacts[i];
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

		// One put somewhere else while the step was solved stays there.
		if (!body.awake || *body.object->transform.position != body.placed || *body.object->transform.rotation != body.turned)
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
			const glm::mat4 above = parent ? parent->WorldMatrix() : glm::mat4(1.0f);
			const bool flat = above == glm::mat4(1.0f);

			glm::mat4 local = glm::mat4_cast(body.orientation);
			local[3] = glm::vec4(body.position, 1.0f);

			if (!flat)
				local = glm::inverse(above) * local;

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

			// Where its colliders are now: under a parent that changes
			// nothing, its own pose and scale, without working the transform
			// just written back out; the next walk finds that as it was left,
			// so keeps this.
			glm::mat4 world = glm::mat4_cast(body.orientation);

			if (flat)
			{
				const glm::vec3 size = body.object->transform.scale;

				for (int axis = 0; axis < glm::vec3::length(); axis++)
					world[axis] *= size[axis];

				world[3] = glm::vec4(body.position, 1.0f);
			}
			else
				world = body.object->WorldMatrix();

			for (Collider* collider : body.colliders)
				m_tree.Measure(*collider, world);

			// What the next step's Weigh would work back out of world.
			if (flat)
			{
				rigidbody.m_posedAt = world;
				rigidbody.m_posedOrientation = body.orientation;
				rigidbody.m_posedScale = body.object->transform.scale;
			};

			body.refit = Outgrown(body, seconds);
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
		if (!body.refit)
			return;

		// Stretched along the travel, so the next steps' motion stays inside.
		for (Collider* collider : body.colliders)
			m_tree.Place(*collider, true, body.velocity * seconds);

		body.refit = false;
	};

	bool Simulation::Outgrown(const Body& body, float seconds) const
	{
		for (const Collider* collider : body.colliders)
			if (!m_tree.Fits(*collider, body.velocity * seconds))
				return true;

		return false;
	};

	int Simulation::Depth(const GameObject& object)
	{
		int depth = 0;

		for (const GameObject* above = object.GetParent(); above != nullptr; above = above->GetParent())
			depth++;

		return depth;
	};

	void Simulation::Keep(size_t begin, size_t end)
	{
		for (size_t i = begin; i < end; i++)
		{
			Pair& pair = m_pairs[i];

			if (!pair.touching)
				continue;

			Candidate& candidate = m_candidates[pair.candidate];

			pair.entering = !candidate.touching;

			candidate.touching = true;
			candidate.trigger = pair.trigger;
			candidate.step = m_step;
			candidate.movableA = At(pair.bodyA).inverseMass > 0.0f;
			candidate.movableB = At(pair.bodyB).inverseMass > 0.0f;
			candidate.kept.resize(pair.count);

			for (size_t k = 0; k < pair.count; k++)
			{
				const Contact& contact = pair.contacts[k];
				Kept& kept = candidate.kept[k];

				kept.point = contact.point;
				kept.normal = contact.normal;

				for (int d = 0; d < DIRECTIONS; d++)
					kept.impulses[d] = contact.axes[d].impulse;
			};
		};
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
			[&](Collider& a, Collider& b, bool aListens, bool bListens, int bodyA, int bodyB, bool trigger, const Contact* contacts, size_t count, Phase phase)
			{
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

		// Those that listen told, in order, of the pairs Keep has kept.
		for (const Pair& pair : m_pairs)
			if (pair.touching && (pair.aListens || pair.bListens))
				report(*pair.a, *pair.b, pair.aListens, pair.bListens, pair.bodyA, pair.bodyB, pair.trigger, pair.contacts, pair.count, pair.entering ? ENTERING : STAYING);

		const auto awake =
			[&](const Collider* collider)
			{
				const int body = BodyOf(*collider);
				return body != NONE && m_bodies[body].awake;
			};

		const auto part =
			[&](Candidate& candidate)
			{
				Collider& a = *const_cast<Collider*>(candidate.key.first);
				Collider& b = *const_cast<Collider*>(candidate.key.second);

				candidate.touching = false;

				if (!candidate.trigger)
				{
					WakeOwner(a);
					WakeOwner(b);
				};

				report(a, b, Listens(*a.m_gameObject), Listens(*b.m_gameObject), BodyOf(a), BodyOf(b), candidate.trigger, nullptr, 0, LEAVING);
			};

		// Taken out first, as a listener may remove a collider, and with it
		// those that name it.
		std::vector<Candidate> parted = std::move(m_parted);
		m_parted.clear();

		for (Candidate& candidate : parted)
			part(candidate);

		// By index, as a listener may remove a collider and its candidates.
		for (size_t i = 0; i < m_candidates.size(); i++)
		{
			Candidate& candidate = m_candidates[i];

			if (!candidate.touching || candidate.step == m_step)
				continue;

			// Asleep on both sides, it is still touching; it was only not looked at.
			if (!awake(candidate.key.first) && !awake(candidate.key.second))
			{
				candidate.step = m_step;
				continue;
			};

			part(candidate);
		};
	};
};
