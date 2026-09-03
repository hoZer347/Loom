#pragma once

#include "Loom API.h"

#include "Macro Helpers.h"

#include "GameObject.h"

#include <thread>
#include <mutex>
#include <latch>
#include <semaphore>
#include <functional>


namespace Loom
{
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
		void Update(int thread = 0);
		void Render();
		void Physics();

		static const std::vector<Scene*>& GetScenes() { return allScenes; };

		static inline std::atomic<bool> is_engine_running = false;
		

	protected:
		friend struct MainMenu;
		friend struct Engine;
		int thread_id;

		GameObject root;

		static inline std::vector<Scene*> allScenes{ };
		static inline std::recursive_mutex mutex{ };
	};
};
