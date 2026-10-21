#pragma once

#include "Component.h"
#include "Scene.h"
#include "Ui.h"

#include <optional>
#include <string>


namespace MainMenuScripts
{
	// What Start Game opens, on the root of its scene. It shows the seed it
	// started on, and Back to Menu loads the menu again.
	struct World : Loom::Component<World>
	{
		static constexpr const char* SCENE = "Scenes/World.loomscene";
		static constexpr const char* MENU = "Scenes/Menu.loomscene";

		// The seed the next World to attach starts on in place of its own,
		// which is how the menu hands one over: a static outlives the scene
		// that set it.
		static inline std::optional<int> nextSeed;

		void OnAttach() override
		{
			if (nextSeed)
			{
				m_seed = *nextSeed;
				nextSeed.reset();
			};

			if (m_seedText != nullptr)
				m_seedText->text = "Seed " + std::to_string(*m_seed);

			if (m_back != nullptr)
				m_back->onClick = []() { Loom::Scene::Load(MENU); };
		};

	private:
		Loom::Serial<int> m_seed = 1;
		Loom::Serial<Loom::UiText*> m_seedText;
		Loom::Serial<Loom::UiButton*> m_back;
	};
};
