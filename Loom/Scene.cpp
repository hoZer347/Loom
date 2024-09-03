import Scene;

import Engine;

import <vector>;

#include "imgui.h"


namespace Loom
{
	Scene::Scene(
		Engine* engine,
		const char* name,
		int thread_id) :
		engine(engine),
		name(name),
		thread_id(thread_id),
		root(engine, nullptr, "Root", thread_id)
	{
		engine->QueueTask(
			[this]()
			{
				allScenes.push_back(this);
			});
	};

	Scene::~Scene()
	{
		engine->QueueTask(
			[this]()
			{
				allScenes.erase(
					std::remove(
						allScenes.begin(), 
						allScenes.end(), this),
					allScenes.end());
			});
	};
};
