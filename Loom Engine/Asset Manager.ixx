export module AssetManager;

import <assimp/Importer.hpp>;
import <unordered_map>;
import <string>;
import <thread>;


namespace Loom
{
	export struct AssetManager final
	{
		static void Start(const std::string& path);
		static void Stop();

	private:
		static inline Assimp::Importer importer{ };
		static inline std::unordered_map<std::string, void*> assets{ };
		static inline bool isRunning = false;
		static inline std::thread thread;

		AssetManager() = delete;
	};
};
