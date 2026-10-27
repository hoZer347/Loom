#include "Cutscenes.h"

#include "GameObject.h"
#include "Ui.h"
#include "Utilities/DialogueManager.h"


namespace Loom
{
	namespace
	{
		// Pinned to the bottom middle of the screen, a margin above its edge.
		constexpr glm::vec2 BOTTOM_MIDDLE = { 0.5f, 1.0f };
		constexpr glm::vec2 MIDDLE = { 0.5f, 0.5f };
		constexpr float MARGIN = 24.0f;
		constexpr float PADDING = 20.0f;

		constexpr glm::vec2 BOX_SIZE = { 720.0f, 180.0f };
		constexpr glm::vec4 BOX_COLOR = { 0.0f, 0.0f, 0.0f, 0.75f };
		constexpr glm::vec4 CLEAR = { 0.0f, 0.0f, 0.0f, 0.0f };
	};

	GameObject* Cutscenes::AddDialogueBox(GameObject& parent, const std::string& font)
	{
		GameObject* box = parent.AddChild(DialogueBoxLabel);

		UiRect* background = box->Attach<UiRect>();
		background->anchor = BOTTOM_MIDDLE;
		background->pivot = BOTTOM_MIDDLE;
		background->offset = glm::vec2(0.0f, -MARGIN);
		background->size = BOX_SIZE;
		background->color = BOX_COLOR;

		box->Attach<DialogueManager>();

		GameObject* line = box->AddChild(DialogueTextLabel);

		UiRect* inset = line->Attach<UiRect>();
		inset->anchor = MIDDLE;
		inset->pivot = MIDDLE;
		inset->size = BOX_SIZE - glm::vec2(PADDING * 2.0f);
		inset->color = CLEAR;
		inset->blocksPointer = false;

		UiText* text = line->Attach<UiText>();
		text->text = std::string();
		text->fontPath = font;
		text->alignment = UiText::Left;
		text->verticalAlignment = UiText::Top;
		text->wrap = true;

		return box;
	};
};
