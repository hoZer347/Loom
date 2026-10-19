#include "doctest.h"

#include "Test Support.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Input.h"
#include "Scene.h"
#include "Ui.h"
#include "Utilities/Clock.h"

#include "glm/glm.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using LoomTests::Pump;

namespace
{
	constexpr int SCREEN_WIDTH = 800;
	constexpr int SCREEN_HEIGHT = 600;
	constexpr int LEFT_BUTTON = 0;

	constexpr float HALF = 0.5f;

	// x, y, z, u, v for each of a quad's six corners.
	constexpr size_t FLOATS_PER_CORNER = 5;
	constexpr size_t CORNERS_PER_QUAD = 6;
	constexpr size_t Y = 1;
	constexpr size_t Z = 2;
	constexpr size_t V = 4;

	constexpr glm::vec4 FIRST_COLOR = { 1.0f, 0.0f, 0.0f, 1.0f };
	constexpr glm::vec4 SECOND_COLOR = { 0.0f, 0.0f, 1.0f, 1.0f };

	// The game drawn in the whole of a window this size, with the cursor at.
	void PointAt(const glm::vec2& at)
	{
		Loom::Input::ClearView();
		Loom::Input::OnMouseMove((int)at.x, (int)at.y, SCREEN_WIDTH, SCREEN_HEIGHT);
	};

	glm::vec2 Middle(const Loom::Ui::Rect& rect)
	{
		return rect.min + rect.size * HALF;
	};

	// The clock's first tick only starts it, so frames are counted rather than ticks.
	void AdvanceFrames(unsigned long long frames)
	{
		const unsigned long long until = Loom::Time::FrameCount() + frames;

		while (Loom::Time::FrameCount() < until)
			Loom::Utilities::Tick();
	};

	// A frame on, with no press still down from the one before.
	void NextFrame()
	{
		Loom::Input::Tick();
		AdvanceFrames(1);
	};

	// A left click this frame hears.
	void Click()
	{
		Loom::Input::OnMouseButton(LEFT_BUTTON, true);
		Loom::Input::OnMouseButton(LEFT_BUTTON, false);
		Loom::Input::Tick();
	};

	// Nothing held, and the overlays of earlier cases forgotten.
	void Settle()
	{
		Loom::Input::OnMouseButton(LEFT_BUTTON, false);
		Loom::Input::Tick();
		Loom::Input::Tick();
		AdvanceFrames(2);
	};

	// The scenes drawn in one overlay, as a view showing the game draws them.
	void Overlay(const std::vector<Loom::Scene*>& scenes)
	{
		Loom::Ui::BeginOverlay();

		for (Loom::Scene* scene : scenes)
			scene->Render(nullptr, scenes);

		Loom::Ui::DrawOverlay();
	};

	std::string WriteFile(const std::string& name, const std::string& contents)
	{
		const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
		std::ofstream(path, std::ios::binary) << contents;
		return path.string();
	};
};


TEST_SUITE("Ui")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the UI components are built in, and the editor can add them by name")
	{
		Loom::Scene scene("ui by name");
		Pump();

		for (const char* name : { "UiRect", "UiText", "UiButton" })
		{
			CAPTURE(name);
			CHECK(Loom::ComponentRegistry::Create(name, scene.GetRoot()) != nullptr);
		};

		Pump();

		CHECK(scene.GetRoot().GetComponent<Loom::UiRect>() != nullptr);
		CHECK(scene.GetRoot().GetComponent<Loom::UiText>() != nullptr);
		CHECK(scene.GetRoot().GetComponent<Loom::UiButton>() != nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a rect's pivot lands on its anchor in the parent, moved by its offset")
	{
		Loom::Scene scene("ui pivot");
		Loom::UiRect& rect = *scene.Attach<Loom::UiRect>();
		Pump();

		// Its top right corner on the parent's, moved left and down.
		rect.anchor = glm::vec2(1.0f, 0.0f);
		rect.pivot = glm::vec2(1.0f, 0.0f);
		rect.offset = glm::vec2(-10.0f, 20.0f);

		const Loom::Ui::Rect parent = { glm::vec2(50.0f, 30.0f), glm::vec2(SCREEN_WIDTH, SCREEN_HEIGHT) };
		const Loom::Ui::Rect laid = rect.LayOut(parent);

		CHECK(laid.min.x + laid.size.x == doctest::Approx(parent.min.x + parent.size.x + rect.offset->x));
		CHECK(laid.min.y == doctest::Approx(parent.min.y + rect.offset->y));
		CHECK(laid.size == *rect.size);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a rect is laid out in its parent's rect, and in the screen when it has none")
	{
		PointAt(glm::vec2(0.0f));

		Loom::Scene scene("ui layout");
		Loom::GameObject* panel = scene.AddChild("Panel");
		Pump();
		Loom::GameObject* item = panel->AddChild("Item");
		Pump();

		Loom::UiRect* outer = panel->Attach<Loom::UiRect>();
		Loom::UiRect* inner = item->Attach<Loom::UiRect>();
		Pump();

		// The panel in the screen's bottom right corner, moved up and left;
		// the item in the middle of the panel.
		outer->anchor = glm::vec2(1.0f, 1.0f);
		outer->pivot = glm::vec2(1.0f, 1.0f);
		outer->offset = glm::vec2(-10.0f, -20.0f);
		outer->size = glm::vec2(200.0f, 100.0f);
		inner->size = glm::vec2(40.0f, 10.0f);

		const Loom::Ui::Rect panelRect = outer->ScreenRect();
		const Loom::Ui::Rect itemRect = inner->ScreenRect();

		CHECK(panelRect.min.x + panelRect.size.x == doctest::Approx(SCREEN_WIDTH + outer->offset->x));
		CHECK(panelRect.min.y + panelRect.size.y == doctest::Approx(SCREEN_HEIGHT + outer->offset->y));
		CHECK(Middle(itemRect).x == doctest::Approx(Middle(panelRect).x));
		CHECK(Middle(itemRect).y == doctest::Approx(Middle(panelRect).y));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the UI of every scene in a view is drawn once they all are, over cleared depth, all at one depth")
	{
		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		Loom::Scene first("ui first");
		Loom::Scene second("ui second");
		Loom::GameObject* a = first.AddChild("A");
		Loom::GameObject* b = second.AddChild("B");
		Pump();

		a->Attach<Loom::UiRect>()->color = FIRST_COLOR;
		b->Attach<Loom::UiRect>()->color = SECOND_COLOR;
		Pump();

		const std::vector<Loom::Scene*> both = { &first, &second };

		Loom::Ui::BeginOverlay();
		first.Render(nullptr, both);
		second.Render(nullptr, both);

		CHECK(renderer.drawn.empty());

		Loom::Ui::DrawOverlay();

		REQUIRE(renderer.drawnColors.size() == 2);
		CHECK(renderer.drawnColors[0] == FIRST_COLOR);
		CHECK(renderer.drawnColors[1] == SECOND_COLOR);
		CHECK(renderer.clears == std::vector<std::pair<bool, bool>>{ { false, true } });
		CHECK(renderer.depthTestsEqual == std::vector<bool>{ true, false });

		for (const std::vector<float>& vertices : renderer.drawnVertices)
			for (size_t corner = 0; corner < vertices.size(); corner += FLOATS_PER_CORNER)
				CHECK(vertices[corner + Z] == renderer.drawnVertices[0][Z]);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a scene rendered outside an overlay leaves its UI out, as the editor's Scene view renders")
	{
		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		Loom::Scene scene("ui outside");
		Loom::GameObject* object = scene.AddChild("Panel");
		Pump();

		object->Attach<Loom::UiRect>();
		Pump();

		scene.Render(nullptr, { &scene });

		Loom::Ui::BeginOverlay();
		Loom::Ui::DrawOverlay();

		CHECK(renderer.drawn.empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a rect with no alpha is not drawn, but still holds the pointer")
	{
		Settle();

		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		Loom::Scene scene("ui clear");
		Loom::GameObject* object = scene.AddChild("Panel");
		Pump();

		Loom::UiRect* rect = object->Attach<Loom::UiRect>();
		Pump();

		rect->color = glm::vec4(0.0f);

		Overlay({ &scene });

		CHECK(renderer.drawn.empty());

		PointAt(Middle(rect->ScreenRect()));

		CHECK(Loom::Ui::IsPointerOver());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a button is hovered and clicked only while the cursor is on its rect")
	{
		Settle();

		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		Loom::Scene scene("ui button");
		Loom::GameObject* object = scene.AddChild("Button");
		Pump();

		Loom::UiRect* rect = object->Attach<Loom::UiRect>();
		Loom::UiButton* button = object->Attach<Loom::UiButton>();
		Pump();

		Overlay({ &scene });

		CHECK_FALSE(button->IsHovered());

		Click();

		CHECK_FALSE(button->WasClicked());

		NextFrame();
		Overlay({ &scene });
		PointAt(Middle(rect->ScreenRect()));

		CHECK(button->IsHovered());
		CHECK_FALSE(button->WasClicked());

		Click();

		CHECK(button->WasClicked());

		Settle();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "of two buttons under the cursor, only the one drawn last reacts")
	{
		Settle();

		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		Loom::Scene scene("ui stacked");
		Loom::GameObject* under = scene.AddChild("Under");
		Loom::GameObject* over = scene.AddChild("Over");
		Pump();

		Loom::UiRect* rect = under->Attach<Loom::UiRect>();
		Loom::UiButton* lower = under->Attach<Loom::UiButton>();
		over->Attach<Loom::UiRect>();
		Loom::UiButton* upper = over->Attach<Loom::UiButton>();
		Pump();

		Overlay({ &scene });
		PointAt(Middle(rect->ScreenRect()));
		Click();

		CHECK(upper->IsHovered());
		CHECK(upper->WasClicked());
		CHECK_FALSE(lower->IsHovered());
		CHECK_FALSE(lower->WasClicked());

		Settle();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a button hidden once it is clicked is not clicked again where it was")
	{
		Settle();

		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		Loom::Scene scene("ui hidden");
		Loom::GameObject* object = scene.AddChild("Button");
		Pump();

		Loom::UiRect* rect = object->Attach<Loom::UiRect>();
		Loom::UiButton* button = object->Attach<Loom::UiButton>();
		Pump();

		Overlay({ &scene });
		PointAt(Middle(rect->ScreenRect()));
		Click();

		REQUIRE(button->WasClicked());

		object->SetDisabled(true);

		NextFrame();
		Overlay({ &scene });
		Click();

		CHECK_FALSE(button->IsHovered());
		CHECK_FALSE(button->WasClicked());

		Settle();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "onClick runs once for a click, on the frame it lands in")
	{
		Settle();

		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		Loom::Scene scene("ui on click");
		Loom::GameObject* object = scene.AddChild("Button");
		Pump();

		Loom::UiRect* rect = object->Attach<Loom::UiRect>();
		Loom::UiButton* button = object->Attach<Loom::UiButton>();
		Pump();

		int clicks = 0;
		button->onClick = [&]() { clicks++; };

		Overlay({ &scene });
		PointAt(Middle(rect->ScreenRect()));
		Click();
		button->OnUpdate();

		CHECK(clicks == 1);

		NextFrame();
		Overlay({ &scene });
		button->OnUpdate();

		CHECK(clicks == 1);

		Settle();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a button's rect is filled with its tint, normal, hovered or pressed")
	{
		Settle();

		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		Loom::Scene scene("ui tint");
		Loom::GameObject* object = scene.AddChild("Button");
		Pump();

		Loom::UiRect* rect = object->Attach<Loom::UiRect>();
		Loom::UiButton* button = object->Attach<Loom::UiButton>();
		Pump();

		Overlay({ &scene });

		REQUIRE(renderer.drawnColors.size() == 1);
		CHECK(renderer.drawnColors[0] == *button->normalColor);
		CHECK(rect->Fill() == *button->normalColor);

		PointAt(Middle(rect->ScreenRect()));

		CHECK(rect->Fill() == *button->hoverColor);

		Loom::Input::OnMouseButton(LEFT_BUTTON, true);
		Loom::Input::Tick();

		CHECK(rect->Fill() == *button->pressedColor);

		Settle();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a rect that does not block the pointer leaves it, and its button's clicks, to the game")
	{
		Settle();

		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		Loom::Scene scene("ui through");
		Loom::GameObject* object = scene.AddChild("Button");
		Pump();

		Loom::UiRect* rect = object->Attach<Loom::UiRect>();
		Loom::UiButton* button = object->Attach<Loom::UiButton>();
		Pump();

		rect->blocksPointer = false;

		Overlay({ &scene });
		PointAt(Middle(rect->ScreenRect()));
		Click();

		CHECK_FALSE(Loom::Ui::IsPointerOver());
		CHECK_FALSE(button->WasClicked());

		Settle();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "what an overlay drew is forgotten once a frame passes without one")
	{
		Settle();

		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		Loom::Scene scene("ui forgotten");
		Loom::GameObject* object = scene.AddChild("Panel");
		Pump();

		Loom::UiRect* rect = object->Attach<Loom::UiRect>();
		Pump();

		Overlay({ &scene });
		PointAt(Middle(rect->ScreenRect()));

		CHECK(Loom::Ui::IsPointerOver());

		AdvanceFrames(1);

		CHECK(Loom::Ui::IsPointerOver());

		AdvanceFrames(1);

		CHECK_FALSE(Loom::Ui::IsPointerOver());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a file that is not a font, or too short to be one, is no font")
	{
		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		constexpr int PIXELS = 2;
		constexpr int RGBA = 4;

		Loom::Scene scene("ui not a font");
		Loom::GameObject* object = scene.AddChild("Label");
		Pump();

		Loom::UiText* text = object->Attach<Loom::UiText>();
		Pump();

		text->fontPath = LoomTests::WriteTga("ui not a font.tga", PIXELS, PIXELS, std::vector<uint8_t>(PIXELS * PIXELS * RGBA, 0));

		CHECK(text->Measure() == glm::vec2(0.0f));

		Overlay({ &scene });

		CHECK(renderer.drawn.empty());

		text->fontPath = WriteFile("ui short font.ttf", "ttf");

		CHECK(text->Measure() == glm::vec2(0.0f));
	};

	// On the desktop only: these bake a TrueType file from the engine's
	// sources, which the web build's virtual file system does not hold.
#ifndef __EMSCRIPTEN__
	const std::string ROBOTO = (std::filesystem::path(__FILE__).parent_path().parent_path() / "External Libraries/imgui/misc/fonts/Roboto-Medium.ttf").string();

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "text is measured in its font")
	{
		LoomTests::RecordingRenderer renderer;

		Loom::Scene scene("ui measure");
		Loom::UiText& text = *scene.Attach<Loom::UiText>();
		Pump();

		text.fontPath = ROBOTO;
		text.text = std::string("Wait");

		const glm::vec2 wait = text.Measure();

		text.text = std::string("Wait a while");

		CHECK(wait.x > 0.0f);
		CHECK(wait.y > 0.0f);
		CHECK(text.Measure().x > wait.x);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a rect's text is drawn over it, whichever was attached first")
	{
		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		Loom::Scene scene("ui text order");
		Loom::GameObject* object = scene.AddChild("Label");
		Pump();

		Loom::UiText* text = object->Attach<Loom::UiText>();
		Loom::UiRect* rect = object->Attach<Loom::UiRect>();
		Pump();

		text->fontPath = ROBOTO;

		Overlay({ &scene });

		REQUIRE(renderer.drawnColors.size() == 2);
		CHECK(renderer.drawnColors[0] == *rect->color);
		CHECK(renderer.drawnColors[1] == *text->color);
		CHECK(renderer.drawnTextures[0] != renderer.drawnTextures[1]);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "text is aligned across its rect and centred down it")
	{
		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		Loom::Scene scene("ui alignment");
		Loom::GameObject* object = scene.AddChild("Label");
		Pump();

		Loom::UiRect* rect = object->Attach<Loom::UiRect>();
		Loom::UiText* text = object->Attach<Loom::UiText>();
		Pump();

		rect->size = glm::vec2(400.0f, 80.0f);
		text->fontPath = ROBOTO;
		text->text = std::string("Wait");

		// The left edge of the first glyph, as the line is drawn aligned so.
		const auto start = [&](Loom::UiText::Alignment alignment)
		{
			text->alignment = alignment;
			Overlay({ &scene });
			return renderer.drawnVertices.back()[0];
		};

		const Loom::Ui::Rect area = rect->ScreenRect();
		const glm::vec2 measured = text->Measure();
		const float room = area.size.x - measured.x;

		// To the pixel, as a glyph is drawn on whole pixels.
		constexpr float PIXEL = 1.0f;

		CHECK(std::abs(start(Loom::UiText::Centre) - start(Loom::UiText::Left) - room * HALF) <= PIXEL);
		CHECK(std::abs(start(Loom::UiText::Right) - start(Loom::UiText::Left) - room) <= PIXEL);

		// Every glyph lies in the line, and the line is centred in the rect.
		const float top = area.min.y + (area.size.y - measured.y) * HALF;
		const std::vector<float>& glyphs = renderer.drawnVertices.back();

		for (size_t corner = 0; corner < glyphs.size(); corner += FLOATS_PER_CORNER)
		{
			CHECK(glyphs[corner + Y] >= top - PIXEL);
			CHECK(glyphs[corner + Y] <= top + measured.y + PIXEL);
		};
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "characters outside printable ASCII are left out of the line")
	{
		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		Loom::Scene scene("ui printable");
		Loom::UiText& text = *scene.Attach<Loom::UiText>();
		Pump();

		constexpr size_t PRINTABLE = 2;

		text.fontPath = ROBOTO;
		text.text = std::string("A\tB\xC3\xA9");

		Overlay({ &scene });

		REQUIRE(!renderer.drawnVertices.empty());
		CHECK(renderer.drawnVertices.back().size() == PRINTABLE * CORNERS_PER_QUAD * FLOATS_PER_CORNER);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a glyph samples the atlas the right way up")
	{
		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		Loom::Scene scene("ui upright");
		Loom::UiText& text = *scene.Attach<Loom::UiText>();
		Pump();

		text.fontPath = ROBOTO;
		text.text = std::string("W");

		Overlay({ &scene });

		REQUIRE(!renderer.drawnVertices.empty());

		// Its first corner is its top left and its third its bottom right; the
		// atlas is bottom row first, so the top samples the higher v.
		constexpr size_t BOTTOM_RIGHT = 2;
		const std::vector<float>& glyph = renderer.drawnVertices.back();

		CHECK(glyph[V] > glyph[BOTTOM_RIGHT * FLOATS_PER_CORNER + V]);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a baked size nothing has drawn for a while is let go, and one still drawn is kept")
	{
		LoomTests::RecordingRenderer renderer;
		PointAt(glm::vec2(0.0f));

		// Sizes no other case bakes, so this one bakes them with this renderer.
		constexpr float UNUSED_SIZE = 37.0f;
		constexpr float USED_SIZE = 38.0f;

		Loom::Scene scene("ui let go");
		Loom::GameObject* left = scene.AddChild("Left");
		Loom::GameObject* kept = scene.AddChild("Kept");
		Pump();

		Loom::UiText* unused = left->Attach<Loom::UiText>();
		Loom::UiText* used = kept->Attach<Loom::UiText>();
		Pump();

		unused->fontPath = ROBOTO;
		unused->fontSize = UNUSED_SIZE;
		used->fontPath = ROBOTO;
		used->fontSize = USED_SIZE;

		Overlay({ &scene });

		REQUIRE(renderer.drawnTextures.size() == 2);
		const uint32_t unusedAtlas = renderer.drawnTextures[0];
		const uint32_t usedAtlas = renderer.drawnTextures[1];

		left->SetDisabled(true);
		AdvanceFrames(Loom::UiText::unusedFrames + 1);
		Overlay({ &scene });

		const auto destroyed = [&](uint32_t texture)
		{
			return std::find(renderer.destroyedTextures.begin(), renderer.destroyedTextures.end(), texture) != renderer.destroyedTextures.end();
		};

		CHECK(destroyed(unusedAtlas));
		CHECK_FALSE(destroyed(usedAtlas));
	};
#endif
};
