#pragma once

#include "Loom API.h"

#include "Macro Helpers.h"

#include "ColliderTree.h"
#include "GameObject.h"
#include "Physics.h"
#include "Simulation.h"

#include <thread>
#include <mutex>
#include <latch>
#include <semaphore>
#include <functional>


namespace Loom
{
	struct Camera;

	struct LOOM_API Scene final :
		public LoomObject
	{
		Scene(const std::string& name = "Scene", int thread_id = 0);
		virtual ~Scene();
		
		template <typename T>
		T* Attach(auto&&... args) { return root.Attach<T>(args...); };
		GameObject* AddChild(const std::string& m_name = "New GameObject") { return root.AddChild(m_name); };

		// The root GameObject every scene hangs off of. Handed out so tools (the
		// editor) can walk the hierarchy without being a friend of Scene.
		GameObject& GetRoot() { return root; };
		const GameObject& GetRoot() const { return root; };

		int GetThreadID() const { return thread_id; };

		// One tick / draw / physics step of the whole hierarchy. Engine calls
		// these every frame; the editor calls Render() itself when it is drawing
		// the scene into its own framebuffer.
		void Update() { Update({ this }); };

		// Each object's OnUpdate on the thread its thread ID names, the caller
		// running thread 0's while the lanes run theirs, and returns once all
		// are done. What each runs is gathered first, so an object disabled or
		// moved mid-update goes by where it was.
		static void Update(const std::vector<Scene*>& scenes);

		// Seen through camera when one is given, in place of the first Camera in
		// the hierarchy: the editor's views that look on from outside the game.
		// A scene with no Camera or Light of its own borrows the first among the
		// open scenes. Shadows are cast by those of drawn, the scenes drawn into
		// the same view, that share its light.
		void Render(Camera* camera = nullptr, const std::vector<Scene*>& drawn = allScenes);

		// Gives the program the view of the scene being drawn: its camera, or
		// world space without one, and its light. Once a pass while Render
		// draws, since a program keeps its uniforms from one draw to the next.
		static void ApplyView(uint32_t program);

		// One physics step: runs OnPhysics through the hierarchy, which syncs
		// every enabled collider into this scene's collider tree and gathers
		// its rigid bodies, then moves them by FixedDeltaTime.
		void Physics();

		// The same, left solving on the simulation's own thread for
		// Simulation::EndAll to finish.
		void BeginPhysics();

		// Against this scene's colliders alone; see Loom::Physics.
		bool Raycast(const Ray& ray, RaycastHit& hit, float maxDistance = INFINITE_DISTANCE) const;
		std::vector<RaycastHit> RaycastAll(const Ray& ray, float maxDistance = INFINITE_DISTANCE) const;

		const ColliderTree& GetColliders() const { return colliders; };

		static const std::vector<Scene*>& GetScenes() { return allScenes; };

		// Puts a scene at index in GetScenes, or last if index is past the end.
		// Deferred, so it lands after a new scene has registered itself.
		static void MoveScene(Scene* scene, size_t index);

		// Takes a scene out of GetScenes for good, so nothing that goes through
		// the open scenes updates, draws or lights by it: a scene only its
		// holder draws, such as the editor's prefab viewer. Deferred, so it
		// lands after the scene has registered itself.
		static void Unlist(Scene* scene);

		// Puts a scene Unlist took out back at the end of GetScenes. Deferred,
		// like Unlist.
		static void Relist(Scene* scene);

		static inline std::atomic<bool> is_engine_running = false;
		

	protected:
		friend struct MainMenu;
		friend struct Engine;
		int thread_id;

		// Walks the hierarchy's OnPhysics into the collider tree and the
		// simulation, ready for a step of either kind.
		void Sync();

		// Before root, so they outlive the colliders root deletes on the spot.
		ColliderTree colliders;
		Simulation simulation;

		GameObject root;

		static inline std::vector<Scene*> allScenes{ };
		static inline std::recursive_mutex mutex{ };

		// The programs ApplyView has given this pass's view, while Render draws.
		static inline std::vector<uint32_t> s_viewed{ };

		// Whether Render is drawing, so ApplyView need give each program its view only once.
		static inline bool s_drawing = false;
	};
};
