import Scene;

import <mutex>;

#include "imgui.h"


namespace Loom
{
	Scene::Scene(const char* name) :
		name(name)
	{
		std::lock_guard lock{ mutex };
		running = true;
		allScenes.push_back(this);
	
		thread = std::thread([this]()
		{
			while (root && running)
			{
				std::lock_guard lock{ mutex };
				root->_Update();
			};
		});
	};

	Scene::~Scene()
	{
		running = false;
		if (thread.joinable())
			thread.join();
	};
};
