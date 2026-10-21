#pragma once

#include "Component.h"
#include "GameObject.h"
#include "Input.h"
#include "Material.h"
#include "Ui.h"
#include "Utilities/Clock.h"

#include "glm/glm.hpp"

#include <string>


namespace UIScripts
{
	// The demo's menu, on the root of its scene. Red, Green and Blue tint the
	// Card, as the project's Red, Green and Blue actions do, More opens the
	// Popup over the menu, and the Popup's Close shuts it again. Clicks counts
	// every click a button takes. The wheel turns the Card a step a notch, and
	// the Turn Left and Turn Right actions turn it while their keys are held.
	struct Menu : Loom::Component<Menu>
	{
		void OnUpdate() override
		{
			if (Chosen("Red"))
				Tint(RED);
			else if (Chosen("Green"))
				Tint(GREEN);
			else if (Chosen("Blue"))
				Tint(BLUE);
			else if (Clicked("More"))
				ShowPopup(true);
			else if (Clicked("Close"))
				ShowPopup(false);

			Turn();
		};

	private:
		static constexpr glm::vec3 RED = { 0.9f, 0.25f, 0.2f };
		static constexpr glm::vec3 GREEN = { 0.3f, 0.75f, 0.35f };
		static constexpr glm::vec3 BLUE = { 0.25f, 0.45f, 0.95f };

		// Degrees.
		static constexpr float TURN_PER_NOTCH = 15.0f;
		static constexpr float TURN_PER_SECOND = 90.0f;

		// The first GameObject called name at or under from.
		static Loom::GameObject* Find(Loom::GameObject& from, const std::string& name)
		{
			if (from.GetName() == name)
				return &from;

			for (Loom::GameObject* child : from.GetChildren())
				if (Loom::GameObject* found = Find(*child, name))
					return found;

			return nullptr;
		};

		bool Clicked(const std::string& name)
		{
			Loom::GameObject* object = Find(*m_gameObject, name);
			const Loom::UiButton* button = object ? object->GetComponent<Loom::UiButton>() : nullptr;

			if (button == nullptr || !button->WasClicked())
				return false;

			m_clicks++;

			if (Loom::GameObject* label = Find(*m_gameObject, "Clicks"))
				if (Loom::UiText* text = label->GetComponent<Loom::UiText>())
					text->text = "Clicks: " + std::to_string(m_clicks);

			return true;
		};

		void Tint(const glm::vec3& color)
		{
			if (Loom::GameObject* card = Find(*m_gameObject, "Card"))
				if (Loom::Material* material = card->GetComponent<Loom::Material>())
					material->color = color;
		};

		// Its button clicked, or a key for the action by the same name pressed.
		bool Chosen(const char* name)
		{
			return Clicked(name) || Loom::InputActions::Down(name);
		};

		// About its upright.
		void Turn()
		{
			float turn = Loom::Input::GetMouseWheel() * TURN_PER_NOTCH;

			if (Loom::InputActions::Held("Turn Left"))
				turn -= TURN_PER_SECOND * Loom::Time::DeltaTime();

			if (Loom::InputActions::Held("Turn Right"))
				turn += TURN_PER_SECOND * Loom::Time::DeltaTime();

			if (turn != 0.0f)
				if (Loom::GameObject* card = Find(*m_gameObject, "Card"))
					card->transform.rotation->y += turn;
		};

		void ShowPopup(bool shown)
		{
			if (Loom::GameObject* popup = Find(*m_gameObject, "Popup"))
				popup->SetDisabled(!shown);
		};

		int m_clicks = 0;
	};
};
