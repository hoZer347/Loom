#include "Scene.h"

#include "Camera.h"
#include "Engine.h"
#include "Light.h"

#include <algorithm>
#include <vector>
#include <string>


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
	
	void Scene::Update(int thread)
	{
		root.Update(thread);
	};

	void Scene::Render()
	{
		// The shadow map has to be finished before the first mesh samples it.
		Light* light = root.FindComponent<Light>();

		if (light)
			light->RenderShadowMap(root);

		Light::current = light;
		Camera::current = root.FindComponent<Camera>();

		root.Render();

		Light::current = nullptr;
		Camera::current = nullptr;
	};

	void Scene::MoveScene(Scene* scene, size_t index)
	{
		Engine::QueueTask(
			[scene, index]()
			{
				if (std::erase(allScenes, scene) == 0)
					return;

				allScenes.insert(
					allScenes.begin() + std::min(index, allScenes.size()),
					scene);
			});
	};

	void Scene::Physics()
	{
		root.Physics();
	};

	Scene::~Scene()
	{
		Engine::QueueTask(
			[this]()
			{
				allScenes.erase(
					std::remove(
						allScenes.begin(),
						allScenes.end(),
						this),
					allScenes.end());
			});
	};
};
