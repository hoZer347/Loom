#include "Scene.h"

#include "Camera.h"
#include "Collider.h"
#include "Engine.h"
#include "Light.h"
#include "Mesh.h"

#include <algorithm>
#include <optional>
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
		simulation(colliders),
		root(nullptr, "Root", thread_id)
	{
		SetName(name);

		colliders.onRemove = [this](const Collider& collider) { simulation.Forget(collider); };

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
		ColliderTree::current = &colliders;
		Simulation::current = &simulation;
		colliders.BeginSync();

		root.Physics();

		colliders.SyncEnlisted();
		colliders.EndSync();
		simulation.Step(Loom::Physics::FixedDeltaTime());

		ColliderTree::current = nullptr;
		Simulation::current = nullptr;
	};

	namespace
	{
		// Normalised, so the tree measures distance along it in world units.
		// Null for a ray going nowhere.
		std::optional<Ray> Normalised(const Ray& ray)
		{
			if (glm::length(ray.direction) == 0.0f)
				return std::nullopt;

			return Ray{ ray.origin, glm::normalize(ray.direction) };
		};
	};

	bool Scene::Raycast(const Ray& given, RaycastHit& hit, float maxDistance) const
	{
		const std::optional<Ray> ray = Normalised(given);
		bool found = false;

		if (!ray)
			return false;

		colliders.Raycast(*ray, maxDistance,
			[&](const RaycastHit& nearer, float)
			{
				hit = nearer;
				found = true;

				return nearer.distance;
			});

		return found;
	};

	std::vector<RaycastHit> Scene::RaycastAll(const Ray& given, float maxDistance) const
	{
		const std::optional<Ray> ray = Normalised(given);
		std::vector<RaycastHit> hits{ };

		if (!ray)
			return hits;

		colliders.Raycast(*ray, maxDistance,
			[&](const RaycastHit& hit, float within)
			{
				hits.push_back(hit);

				return within;
			});

		std::sort(hits.begin(), hits.end(),
			[](const RaycastHit& a, const RaycastHit& b) { return a.distance < b.distance; });

		return hits;
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
