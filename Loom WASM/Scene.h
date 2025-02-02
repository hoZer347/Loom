#include "Macro Helpers.h"

#include "GameObject.h"

#include <thread>
#include <mutex>
#include <latch>
#include <semaphore>
#include <functional>


namespace Loom
{
	struct Scene final :
		public LoomObject
	{
		Scene(const std::string& name = "Scene", int thread_id = 0);
		virtual ~Scene();
		
		template <typename T>
		T* Attach(auto&&... args) { return root.Attach<T>(args...); };
		GameObject* AddChild(const std::string& m_name = "New GameObject") { return root.AddChild(m_name); };

		static inline std::atomic<bool> is_engine_running = false;

	protected:
		friend struct MainMenu;
		friend struct Engine;
		int thread_id;

		GameObject root;

		static inline std::recursive_mutex mutex{ };
		static inline std::vector<Scene*> allScenes{ };
	};
};
