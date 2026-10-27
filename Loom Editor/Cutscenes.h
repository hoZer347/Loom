#pragma once

#include <string>


namespace Loom
{
	struct GameObject;

	/**
	* Loom::Cutscenes
	* - What the Hierarchy's Cutscene menu makes
	*/
	namespace Cutscenes
	{
		constexpr const char* DialogueBoxLabel = "Dialogue Box";
		constexpr const char* DialogueTextLabel = "Text";

		// A child of parent along the bottom of the screen: a UiRect for its
		// background with a DialogueManager beside it, streaming its dialogue
		// into the wrapping UiText of the child inset in it, drawn in font.
		GameObject* AddDialogueBox(GameObject& parent, const std::string& font);
	};
};
