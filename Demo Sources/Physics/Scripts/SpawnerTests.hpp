#pragma once

#include "Spawner.hpp"

#include "Engine.h"
#include "Scene.h"
#include "ScriptTests.h"

#include "glm/glm.hpp"


namespace PhysicsScripts
{
	// Cases for the Tests panel. Each builds a scene of its own, far above
	// the one open in the editor so its bodies meet only each other.
	namespace SpawnerTests
	{
		constexpr glm::vec3 ORIGIN{ 0.0f, 1000.0f, 0.0f };

		inline Spawner* PlaceSpawner(Loom::Scene& scene)
		{
			Loom::GameObject* object = scene.AddChild("Spawner");
			Loom::Engine::DoTasks();

			Spawner* spawner = object->Attach<Spawner>();
			Loom::Engine::DoTasks();

			return spawner;
		};

		LOOM_TEST("The pile never holds more than most bodies")
		{
			constexpr int PAST_MOST = 5;

			Loom::Scene scene("Spawner Tests");
			Loom::Engine::DoTasks();

			Spawner* spawner = PlaceSpawner(scene);

			for (int i = 0; i < spawner->Most() + PAST_MOST; i++)
				spawner->Drop(ORIGIN);

			Loom::Engine::DoTasks();
			spawner->Tidy();
			Loom::Engine::DoTasks();

			LOOM_CHECK((int)spawner->Bodies().size() == spawner->Most());
			LOOM_CHECK((int)spawner->GetGameObject()->GetChildren().size() == spawner->Most());
		};

		LOOM_TEST("A body that falls below the world is let go")
		{
			constexpr float UNDER = 1.0f;

			Loom::Scene scene("Spawner Tests");
			Loom::Engine::DoTasks();

			Spawner* spawner = PlaceSpawner(scene);

			spawner->Drop(ORIGIN);
			spawner->Drop(glm::vec3(0.0f, Spawner::LOST - UNDER, 0.0f));
			Loom::Engine::DoTasks();

			spawner->Tidy();
			Loom::Engine::DoTasks();

			LOOM_CHECK(spawner->Bodies().size() == 1);
			LOOM_CHECK(spawner->Bodies().front()->transform.position->y > Spawner::LOST);
		};
	};
};
