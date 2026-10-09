#include "Scene.h"

#include "Camera.h"
#include "Engine.h"
#include "Light.h"
#include "Mesh.h"

#include <algorithm>
#include <vector>
#include <string>


namespace Loom
{
	namespace
	{
		template <typename T>
		T* FirstAmong(const std::vector<Scene*>& scenes)
		{
			for (Scene* scene : scenes)
				if (T* found = scene->GetRoot().FindComponent<T>())
					return found;

			return nullptr;
		};
	};

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

	void Scene::Render(Camera* camera)
	{
		Camera* seen = camera ? camera : root.FindComponent<Camera>();

		// A scene whose cameras are all disabled shows nothing.
		if (seen == nullptr && root.FindComponent<Camera>(true) != nullptr)
			return;

		// Scenes open together are one world: one with no camera or light of
		// its own is seen through and lit by the first among them, and drawn in
		// world space when no scene has a camera.
		if (seen == nullptr)
			seen = FirstAmong<Camera>(allScenes);

		Light* light = root.FindComponent<Light>();

		if (light == nullptr && root.FindComponent<Light>(true) == nullptr)
			light = FirstAmong<Light>(allScenes);

		// The shadow map has to be finished before the first mesh samples it.

		if (light)
			light->RenderShadowMap(root);

		Light::current = light;
		Camera::current = seen;

		std::vector<Mesh*> transparent;
		Mesh::transparent = &transparent;

		root.Render();

		Mesh::transparent = nullptr;
		Mesh::DrawTransparent(transparent);

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
