#pragma once

#include "Component.h"
#include "GameObject.h"
#include "Material.h"
#include "Ui.h"

#include "glm/glm.hpp"

#include <string>


namespace UIScripts
{
	// The demo's menu, on the root of its scene. Red, Green and Blue tint the
	// Card, More opens the Popup over the menu, and the Popup's Close shuts
	// it again. Clicks counts every click a button takes.
	struct Menu : Loom::Component<Menu>
	{
		void OnUpdate() override
		{
			if (Clicked("Red"))
				Tint(RED);
			else if (Clicked("Green"))
				Tint(GREEN);
			else if (Clicked("Blue"))
				Tint(BLUE);
			else if (Clicked("More"))
				ShowPopup(true);
			else if (Clicked("Close"))
				ShowPopup(false);
		};

	private:
		static constexpr glm::vec3 RED = { 0.9f, 0.25f, 0.2f };
		static constexpr glm::vec3 GREEN = { 0.3f, 0.75f, 0.35f };
		static constexpr glm::vec3 BLUE = { 0.25f, 0.45f, 0.95f };

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

		void ShowPopup(bool shown)
		{
			if (Loom::GameObject* popup = Find(*m_gameObject, "Popup"))
				popup->SetDisabled(!shown);
		};

		int m_clicks = 0;
	};
};
