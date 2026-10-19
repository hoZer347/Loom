#include "doctest.h"

#include "Test Support.h"

#include "Camera.h"
#include "GameObject.h"
#include "Input.h"
#include "Scene.h"
#include "Ui.h"
#include "Utilities/Clock.h"

#include "glm/glm.hpp"

#include <filesystem>
#include <string>

using LoomTests::Pump;

namespace
{
	constexpr int SCREEN_WIDTH = 800;
	constexpr int SCREEN_HEIGHT = 600;
	constexpr int LEFT_BUTTON = 0;

	// The game drawn in the whole of a window this size, with the cursor at x, y.
	void PointAt(float x, float y)
	{
		Loom::Input::ClearView();
		Loom::Input::OnMouseMove((int)x, (int)y, SCREEN_WIDTH, SCREEN_HEIGHT);
	};

	void Click()
	{
		Loom::Input::OnMouseButton(LEFT_BUTTON, true);
		Loom::Input::OnMouseButton(LEFT_BUTTON, false);
		Loom::Input::Tick();
	};

	// The clock's first tick only starts it, so frames are counted rather than ticks.
	void AdvanceFrames(unsigned long long frames)
	{
		const unsigned long long until = Loom::Time::FrameCount() + frames;

		while (Loom::Time::FrameCount() < until)
			Loom::Utilities::Tick();
	};

	// Runs out any press so the next case starts with nothing down.
	void Settle()
	{
		Loom::Input::OnMouseButton(LEFT_BUTTON, false);
		Loom::Input::Tick();
		Loom::Input::Tick();
	};
};


TEST_SUITE("Ui")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a rect's pivot lands on its anchor in the parent, moved by its offset")
	{
		Loom::Scene scene("ui pivot");
		Loom::UiRect& rect = *scene.Attach<Loom::UiRect>();
		Pump();

		rect.anchor = glm::vec2(1.0f, 0.0f);
		rect.pivot = glm::vec2(1.0f, 0.0f);
		rect.offset = glm::vec2(-10.0f, 20.0f);
		rect.size = glm::vec2(100.0f, 50.0f);

		const Loom::Ui::Rect laid = rect.LayOut({ glm::vec2(0.0f), glm::vec2(SCREEN_WIDTH, SCREEN_HEIGHT) });

		CHECK(laid.min.x == doctest::Approx(SCREEN_WIDTH - 10.0f - 100.0f));
		CHECK(laid.min.y == doctest::Approx(20.0f));
		CHECK(laid.size.x == doctest::Approx(100.0f));
		CHECK(laid.size.y == doctest::Approx(50.0f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a rect is laid out in its parent's rect, and the screen when it has none")
	{
		PointAt(0.0f, 0.0f);

		Loom::Scene scene("ui layout");
		Loom::GameObject* panel = scene.AddChild("Panel");
		Pump();
		Loom::GameObject* item = panel->AddChild("Item");
		Pump();

		Loom::UiRect* outer = panel->Attach<Loom::UiRect>();
		Loom::UiRect* inner = item->Attach<Loom::UiRect>();
		Pump();

		outer->anchor = glm::vec2(0.0f, 0.0f);
		outer->pivot = glm::vec2(0.0f, 0.0f);
		outer->offset = glm::vec2(100.0f, 50.0f);
		outer->size = glm::vec2(200.0f, 100.0f);

		inner->anchor = glm::vec2(0.5f, 1.0f);
		inner->pivot = glm::vec2(0.5f, 1.0f);
		inner->size = glm::vec2(40.0f, 10.0f);

		const Loom::Ui::Rect laid = inner->ScreenRect();

		CHECK(laid.min.x == doctest::Approx(100.0f + 100.0f - 20.0f));
		CHECK(laid.min.y == doctest::Approx(50.0f + 100.0f - 10.0f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a button is clicked by a left click on its rect, and tints as the cursor is over it")
	{
		Settle();

		Loom::Scene scene("ui button");
		Loom::GameObject* object = scene.AddChild("Button");
		Pump();

		Loom::UiRect* rect = object->Attach<Loom::UiRect>();
		Loom::UiButton* button = object->Attach<Loom::UiButton>();
		Pump();

		int clicks = 0;
		button->onClick = [&]() { clicks++; };

		// 160 by 40 in the middle of the screen.
		PointAt(10.0f, 10.0f);
		button->OnUpdate();

		CHECK_FALSE(button->IsHovered());
		CHECK(*rect->color == *button->normalColor);

		PointAt(SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f);
		button->OnUpdate();

		CHECK(button->IsHovered());
		CHECK_FALSE(button->WasClicked());
		CHECK(*rect->color == *button->hoverColor);

		Click();
		button->OnUpdate();

		CHECK(button->WasClicked());
		CHECK(clicks == 1);

		Loom::Input::Tick();
		button->OnUpdate();

		CHECK_FALSE(button->WasClicked());
		CHECK(clicks == 1);

		Settle();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a click off a button does not click it")
	{
		Settle();

		Loom::Scene scene("ui miss");
		Loom::GameObject* object = scene.AddChild("Button");
		Pump();

		object->Attach<Loom::UiRect>();
		Loom::UiButton* button = object->Attach<Loom::UiButton>();
		Pump();

		PointAt(10.0f, 10.0f);
		Click();
		button->OnUpdate();

		CHECK_FALSE(button->WasClicked());

		Settle();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the pointer is over the UI while it is on a rect that blocks it, and not two frames after it leaves")
	{
		Loom::Scene scene("ui pointer");
		Loom::GameObject* object = scene.AddChild("Panel");
		Pump();

		Loom::UiRect* rect = object->Attach<Loom::UiRect>();
		Pump();

		PointAt(SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f);
		rect->OnUpdate();

		CHECK(Loom::Ui::IsPointerOver());

		PointAt(10.0f, 10.0f);
		AdvanceFrames(2);
		rect->OnUpdate();

		CHECK_FALSE(Loom::Ui::IsPointerOver());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a rect that does not block the pointer leaves it to the game")
	{
		AdvanceFrames(2);

		Loom::Scene scene("ui clear");
		Loom::GameObject* object = scene.AddChild("Panel");
		Pump();

		Loom::UiRect* rect = object->Attach<Loom::UiRect>();
		Pump();

		rect->blocksPointer = false;

		PointAt(SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f);
		rect->OnUpdate();

		CHECK_FALSE(Loom::Ui::IsPointerOver());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene seen through its own camera draws its UI in front of it, and through the editor's does not")
	{
		LoomTests::RecordingRenderer renderer;
		PointAt(0.0f, 0.0f);

		Loom::Scene scene("ui overlay");
		Loom::Camera* camera = scene.Attach<Loom::Camera>();
		Loom::GameObject* object = scene.AddChild("Panel");
		Pump();

		object->Attach<Loom::UiRect>();
		Pump();

		scene.Render(camera, { &scene });

		CHECK(renderer.drawn.empty());

		scene.Render(nullptr, { &scene });

		REQUIRE(renderer.drawn.size() == 1);
		REQUIRE(!renderer.lastVertices.empty());

		// x, y, z, u, v: the first corner is the rect's top left, nearer than the scene.
		CHECK(renderer.lastVertices[0] == doctest::Approx(SCREEN_WIDTH / 2.0f - 80.0f));
		CHECK(renderer.lastVertices[1] == doctest::Approx(SCREEN_HEIGHT / 2.0f - 20.0f));
		CHECK(renderer.lastVertices[2] < -0.99f);
	};

#ifndef __EMSCRIPTEN__
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "text is measured in its font, and as nothing when the font will not load")
	{
		LoomTests::RecordingRenderer renderer;

		Loom::Scene scene("ui text");
		Loom::UiText& text = *scene.Attach<Loom::UiText>();
		Pump();

		text.fontPath = (std::filesystem::path(__FILE__).parent_path().parent_path() / "External Libraries/imgui/misc/fonts/Roboto-Medium.ttf").string();
		text.fontSize = 32.0f;
		text.text = std::string("Wait");

		const glm::vec2 wait = text.Measure();

		text.text = std::string("Wait a while");

		CHECK(wait.x > 0.0f);
		CHECK(wait.y > 0.0f);
		CHECK(text.Measure().x > wait.x);

		text.fontPath = std::string("no such font.ttf");

		CHECK(text.Measure() == glm::vec2(0.0f));
	};
#endif
};
