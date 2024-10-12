#include <filesystem>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

import AssetManager;

import <iostream>;
import <fstream>;
import <chrono>;

using namespace std::chrono_literals;


namespace Loom
{
	void AssetManager::Start(const std::string& path)
	{
		isRunning = true;

		thread = std::thread(
			[&]()
			{
				std::cout << path << std::endl;

				if (std::filesystem::exists(path))
					std::cout << "Path exists: " << path << std::endl;
				else
					std::cout << "Path does not exist: " << path << std::endl;

				while (isRunning)
				{
					std::filesystem::directory_iterator dir_iter{ std::filesystem::path(path) };

					for (const auto& entry : dir_iter)
					{
						if (entry.path().extension() == ".obj")
						{
							const aiScene* scene =
								importer.ReadFile(
									entry.path().string().c_str(),
									aiProcess_Triangulate | aiProcess_FlipUVs);

							scene->mNumMeshes;
							scene->mMeshes[0]->mVertices[0];
						};
					};
				};
			});
	};

	void AssetManager::Stop()
	{
		isRunning = false;
		if (thread.joinable())
			thread.join();
	};
};
