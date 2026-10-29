#include "Scene.h"

#include "Camera.h"
#include "Collider.h"
#include "ComponentRegistry.h"
#include "Engine.h"
#include "Jobs.h"
#include "Lane.h"
#include "Light.h"
#include "Mesh.h"
#include "Profiler.h"
#include "Renderer.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <vector>
#include <string>


namespace Loom
{
	namespace
	{
		#pragma region Lights

		template <typename T>
		T* FirstAmong(const std::vector<Scene*>& scenes)
		{
			for (Scene* scene : scenes)
				if (T* found = scene->GetRoot().FindComponent<T>())
					return found;

			return nullptr;
		};

		// The scene's own light, else the first among scenes; one with all lights off stays unlit.
		Light* LightFor(Scene& scene, const std::vector<Scene*>& scenes)
		{
			if (Light* own = scene.GetRoot().FindComponent<Light>())
				return own;

			return scene.GetRoot().FindComponent<Light>(true) == nullptr
				? FirstAmong<Light>(scenes)
				: nullptr;
		};

		#pragma endregion

		#pragma region Updates

		// Each lane's updates, 0 the caller, kept so gathering does not allocate.
		std::vector<std::vector<ComponentBase*>> s_updates(1);

		// Lane n at index n - 1, made on first use.
		std::vector<std::unique_ptr<Lane>> s_lanes{ };

		// Components that threw, reported once, under a lock so lines do not interleave.
		std::mutex s_threwMutex;
		std::unordered_set<uint64_t> s_threw;

		void ReportThrow(const ComponentBase& component, const std::string& what)
		{
			std::lock_guard lock(s_threwMutex);

			if (s_threw.insert(component.m_ID).second)
				std::cerr << ComponentRegistry::NameOf(component) << " OnUpdate threw" << what << std::endl;
		};

		void RunUpdates(const std::vector<ComponentBase*>& updates)
		{
			LOOM_STAGE("Scene::Update");

			// A throw stays with its script; the rest still update.
			for (ComponentBase* updateable : updates)
				try { updateable->OnUpdate(); }
				catch (const std::exception& e)
				{
					ReportThrow(*updateable, std::string(": ") + e.what());
				}
				catch (...)
				{
					ReportThrow(*updateable, " an unknown exception");
				};
		};

		#pragma endregion
	};

	#pragma region Lifetime

	Scene::Scene(
		const std::string& name,
		int thread_id) :
		thread_id(thread_id),
		simulation(colliders),
		root(nullptr, "Root", thread_id)
	{
		SetName(name);

		colliders.onRemove = [this](const Collider& collider) { simulation.Forget(collider); };
		colliders.onChange = [this]() { simulation.Land(); };

		Engine::QueueTask(
			[this]()
			{
				allScenes.push_back(this);
			});
	};

	Scene::~Scene()
	{
		simulation.Drop();

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

	#pragma endregion

	#pragma region Frame

	void Scene::Update(const std::vector<Scene*>& scenes)
	{
		for (std::vector<ComponentBase*>& updates : s_updates)
			updates.clear();

		{
			LOOM_STAGE("GameObject::Gather");

			for (Scene* scene : scenes)
				scene->root.Gather(s_updates, Jobs::Cores());
		};

		for (size_t number = 1; number < s_updates.size(); number++)
		{
			if (s_updates[number].empty())
				continue;

			if (s_lanes.size() < number)
				s_lanes.resize(number);

			std::unique_ptr<Lane>& lane = s_lanes[number - 1];

			// After the job workers in the profiler.
			if (!lane)
				lane = std::make_unique<Lane>("Update " + std::to_string(number), (int)(Jobs::Cores() + number));

			lane->Start([&updates = s_updates[number]]() { RunUpdates(updates); });
		};

		RunUpdates(s_updates[0]);

		for (const std::unique_ptr<Lane>& lane : s_lanes)
			if (lane)
				lane->Wait();
	};

	void Scene::Render(Camera* camera, const std::vector<Scene*>& drawn)
	{
		LOOM_STAGE("Scene::Render");

		Camera* seen = camera ? camera : root.FindComponent<Camera>();

		// A scene whose cameras are all disabled shows nothing.
		if (seen == nullptr && root.FindComponent<Camera>(true) != nullptr)
			return;

		// Open scenes are one world, sharing the first camera and light among them.
		if (seen == nullptr)
			seen = FirstAmong<Camera>(allScenes);

		Light* light = LightFor(*this, allScenes);

		// The shadow map is finished first, holding every visible scene the light falls on.
		if (light)
		{
			LOOM_STAGE("Light::RenderShadowMap");

			std::vector<GameObject*> lit{ &root };

			for (Scene* scene : drawn)
				if (scene != this && LightFor(*scene, allScenes) == light)
					lit.push_back(&scene->root);

			light->RenderShadowMap(lit);
		};

		Light::current = light;
		Camera::current = seen;

		// Programs drawn by the last pass, or between passes, hold another view.
		s_viewed.clear();
		s_drawing = true;

		std::vector<Mesh*> transparent;
		MeshBatch batch;
		Mesh::transparent = &transparent;
		Mesh::batch = &batch;

		{
			LOOM_STAGE("GameObject::Render");
			root.Render();
		};

		Mesh::transparent = nullptr;
		Mesh::batch = nullptr;

		{
			LOOM_STAGE("MeshBatch::Draw");
			batch.Draw(Mesh::Ready);

			if (Renderer* renderer = Renderer::Get())
				renderer->SetCullFace(CullFace::None);
		};

		{
			LOOM_STAGE("Mesh::DrawTransparent");
			Mesh::DrawTransparent(transparent);
		};

		s_drawing = false;

		Light::current = nullptr;
		Camera::current = nullptr;
	};

	void Scene::ApplyView(uint32_t program)
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer)
			return;

		if (s_drawing)
		{
			if (std::ranges::find(s_viewed, program) != s_viewed.end())
				return;

			s_viewed.push_back(program);
		};

		if (Camera::current)
			Camera::current->Apply(program);
		// Without a camera, world space is what the screen shows.
		else renderer->SetUniform(program, "u_viewProjection", glm::mat4(1.0f));

		if (Light::current)
			Light::current->Apply(program);
		else Light::ApplyNone(program);
	};

	#pragma endregion

	#pragma region Physics

	void Scene::Physics()
	{
		LOOM_STAGE("Scene::Physics");

		Sync();
		simulation.Step(Loom::Physics::FixedDeltaTime());
	};

	void Scene::BeginPhysics()
	{
		LOOM_STAGE("Scene::BeginPhysics");

		Sync();
		simulation.Begin(Loom::Physics::FixedDeltaTime());
	};

	void Scene::Sync()
	{
		// The walk changes the tree, which a step still solving reads.
		simulation.End();

		ColliderTree::current = &colliders;
		Simulation::current = &simulation;

		{
			LOOM_STAGE("ColliderTree::BeginSync");
			colliders.BeginSync();
		};

		{
			LOOM_STAGE("GameObject::Physics");
			root.Physics();
		};

		{
			LOOM_STAGE("ColliderTree::SyncEnlisted");
			colliders.SyncEnlisted();
		};

		{
			LOOM_STAGE("ColliderTree::EndSync");
			colliders.EndSync();
		};

		ColliderTree::current = nullptr;
		Simulation::current = nullptr;
	};

	namespace
	{
		// Normalised so distances are in world units; null for a zero ray.
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

		const std::shared_lock asking(ColliderTree::guard);

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

		const std::shared_lock asking(ColliderTree::guard);

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

	#pragma endregion

	#pragma region Scene List

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

	void Scene::Unlist(Scene* scene)
	{
		Engine::QueueTask([scene]() { std::erase(allScenes, scene); });
	};

	void Scene::Relist(Scene* scene)
	{
		Engine::QueueTask(
			[scene]()
			{
				if (std::find(allScenes.begin(), allScenes.end(), scene) == allScenes.end())
					allScenes.push_back(scene);
			});
	};

	#pragma endregion
};
