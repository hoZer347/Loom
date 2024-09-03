export module Scene;

import GameObject;

import <thread>;
import <mutex>;
import <latch>;
import <semaphore>;
import <functional>;


namespace Loom
{
	export struct Engine;
	
	export struct Scene final
	{
		Scene(Engine* engine, const char* name, int thread_id = 0);
		virtual ~Scene();

		template <typename T>
		T* Attach(auto&&... args) { return root.Attach<T>(args...); };

		GameObject* AddChild(const char* name = "New GameObject") { return root.AddChild(name); };

		const char* name;

		static inline std::counting_semaphore semaphore{ 4 };
		static inline std::atomic<bool> is_engine_running = false;

	protected:
		friend struct Engine;
		Engine* engine;
		int thread_id;

		GameObject root;

		GameObject root;

		static inline std::recursive_mutex mutex{ };
		static inline std::vector<Scene*> allScenes{ };
	};
};
