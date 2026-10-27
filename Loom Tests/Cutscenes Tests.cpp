#include "doctest.h"

#include "Test Support.h"

#include "Cutscenes.h"

#include "GameObject.h"
#include "Scene.h"
#include "Ui.h"
#include "Utilities/DialogueManager.h"

#include <string>

using LoomTests::Pump;


TEST_SUITE("Cutscenes")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a dialogue box is a background with a manager, streaming into the wrapping text inside it")
	{
		const std::string font = "Assets/Roboto.ttf";

		Loom::Scene scene("cutscene dialogue box");
		Pump();

		Loom::GameObject* box = Loom::Cutscenes::AddDialogueBox(scene.GetRoot(), font);
		Pump();

		REQUIRE(box != nullptr);
		CHECK(box->GetName() == Loom::Cutscenes::DialogueBoxLabel);
		CHECK(box->GetParent() == &scene.GetRoot());

		const Loom::UiRect* background = box->GetComponent<Loom::UiRect>();

		REQUIRE(background != nullptr);
		CHECK(background->color->a > 0.0f);
		CHECK(background->anchor->y == 1.0f);
		CHECK(box->GetComponent<Loom::Basic::DialogueManager>() != nullptr);
		CHECK(box->GetComponent<Loom::UiText>() == nullptr);

		REQUIRE(box->GetChildren().size() == 1);

		Loom::GameObject* line = box->GetChildren().front();
		const Loom::UiText* text = line->GetComponent<Loom::UiText>();

		CHECK(line->GetName() == Loom::Cutscenes::DialogueTextLabel);
		REQUIRE(text != nullptr);
		CHECK(*text->fontPath == font);
		CHECK(text->text->empty());
		CHECK(*text->wrap);
		CHECK(*text->alignment == Loom::UiText::Left);
		CHECK(*text->verticalAlignment == Loom::UiText::Top);

		const Loom::UiRect* inset = line->GetComponent<Loom::UiRect>();

		REQUIRE(inset != nullptr);
		CHECK(inset->color->a == 0.0f);
		CHECK_FALSE(*inset->blocksPointer);
		CHECK(inset->size->x < background->size->x);
		CHECK(inset->size->y < background->size->y);
	};
};
