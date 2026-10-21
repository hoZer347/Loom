#pragma once

#include "Component.h"
#include "Engine.h"
#include "Scene.h"
#include "Ui.h"

#include "World.hpp"

#include <random>


namespace MainMenuScripts
{
	// The title screen, on the root of its scene. Start Game loads the world
	// on a new seed, and Quit ends the game, which in the editor ends the run.
	struct Menu : Loom::Component<Menu>
	{
		void OnAttach() override
		{
			if (m_start != nullptr)
				m_start->onClick = []()
				{
					World::nextSeed = (int)(std::random_device{ }() % SEEDS);
					Loom::Scene::Load(World::SCENE);
				};

			if (m_quit != nullptr)
				m_quit->onClick = []() { Loom::Engine::Quit(); };
		};

	private:
		// How many seeds a new game picks among.
		static constexpr unsigned int SEEDS = 1000000;

		Loom::Serial<Loom::UiButton*> m_start;
		Loom::Serial<Loom::UiButton*> m_quit;
	};
};
