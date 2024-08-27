export module Scene;

import GameObject;

import <thread>;
import <mutex>;
import <functional>;


namespace Loom
{
	export struct Scene final
	{
		Scene(const char* name);
		virtual ~Scene();

		const char* name;

		GameObject* root = new GameObject("Root");

	protected:
		friend struct Engine;

		static inline std::recursive_mutex mutex{ };
		static inline std::vector<Scene*> allScenes{ };

	private:
		bool running = false;
		std::thread thread;
	};
};
