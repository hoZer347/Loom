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

		// The scene's own light, or with none at all, the first among scenes.
		// One whose own lights are all off stays unlit.
		Light* LightFor(Scene& scene, const std::vector<Scene*>& scenes)
		{
			if (Light* own = scene.GetRoot().FindComponent<Light>())
				return own;

			return scene.GetRoot().FindComponent<Light>(true) == nullptr
				? FirstAmong<Light>(scenes)
				: nullptr;
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

	void Scene::Render(Camera* camera, const std::vector<Scene*>& drawn)
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

		Light* light = LightFor(*this, allScenes);

		// The shadow map has to be finished before the first mesh samples it,
		// and holds every scene drawn alongside this one that the light falls
		// on, so shadows cross between them but not from scenes out of view.
		if (light)
		{
			std::vector<GameObject*> lit{ &root };

			for (Scene* scene : drawn)
				if (scene != this && LightFor(*scene, allScenes) == light)
					lit.push_back(&scene->root);

			light->RenderShadowMap(lit);
		};

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
