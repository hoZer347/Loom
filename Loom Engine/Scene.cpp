import Scene;

import Serialize;
import Engine;

import <vector>;
import <string>;

#include "imgui.h"


namespace Loom
{
	Scene::Scene(
		const std::string& name,
		int thread_id) :
		thread_id(thread_id),
		root(nullptr, "Root", thread_id)
	{
		SetName(name);

		Engine::QueueTask(
			[this]()
			{
				allScenes.push_back(this);
			});
	};

	Scene::~Scene()
	{
		Engine::QueueTask(
			[this]()
			{
				allScenes.erase(
					std::remove(
						allScenes.begin(), 
						allScenes.end(), this),
					allScenes.end());
			});
	};

	void Scene::OnSerialize()
	{
		Serialize::Push(&root);
	};
};
