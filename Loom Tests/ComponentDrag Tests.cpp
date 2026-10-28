#include "doctest.h"

#include "Test Support.h"

#include "ComponentDrag.h"
#include "GameObject.h"
#include "Scene.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <array>
#include <cmath>

using LoomTests::Pump;
namespace ComponentDrag = Loom::ComponentDrag;


#pragma region Helpers

namespace
{
	constexpr ImGuiTreeNodeFlags header_flags = ImGuiTreeNodeFlags_DefaultOpen;

	constexpr std::array<const char*, 3> labels = { "Transform", "Mesh", "Material" };
	constexpr size_t mesh = 1;
	constexpr size_t none = labels.size();

	using Open = std::array<bool, labels.size()>;

	constexpr Open all_open = { true, true, true };
	constexpr Open all_folded = { false, false, false };
	constexpr Open mesh_folded = { true, false, true };

	// An ImGui context with an Inspector to draw the headers in, a frame at a time.
	struct Inspector
	{
		Inspector()
		{
			constexpr float width = 800.0f;
			constexpr float height = 600.0f;
			constexpr float frame_time = 1.0f / 60.0f;

			context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(width, height);
			io.DeltaTime = frame_time;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);
		};

		~Inspector()
		{
			ImGui::DestroyContext(context);
		};

		// Which headers are open in a frame drawn folded or not, with the one
		// at byHand folded by the user as it is drawn.
		static Open Frame(bool folded, size_t byHand = none)
		{
			constexpr float window_width = 400.0f;
			constexpr float window_height = 300.0f;

			Open open{ };

			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(window_width, window_height));
			ImGui::Begin("Inspector");

			for (size_t i = 0; i < labels.size(); i++)
			{
				if (i == byHand)
					ImGui::SetNextItemOpen(false);

				open[i] = ComponentDrag::Header(labels[i], header_flags, folded);
			};

			ImGui::End();
			ImGui::EndFrame();

			return open;
		};

		ImGuiContext* context;
	};

	// Two GameObjects with a component each, as the Inspector shows one of them.
	struct TwoObjects : LoomTests::EngineFixture
	{
		TwoObjects()
		{
			Pump();
			shown = scene.GetRoot().AddChild("Shown");
			other = scene.GetRoot().AddChild("Other");
			Pump();
			own = shown->Attach<LoomTests::InertComponent>();
			others = other->Attach<LoomTests::InertComponent>();
			Pump();
		};

		Loom::Scene scene{ "component drag" };
		Loom::GameObject* shown = nullptr;
		Loom::GameObject* other = nullptr;
		Loom::ComponentBase* own = nullptr;
		Loom::ComponentBase* others = nullptr;
	};

	// An Inspector 400 pixels tall with 10 pixel rows, so its edge bands are
	// 20 pixels deep and half a second of holding scrolls 60 pixels.
	constexpr float row = 10.0f;
	constexpr float half_second = 0.5f;
	constexpr float half_second_pixels = half_second * ComponentDrag::rows_per_second * row;

	const ImRect inner(ImVec2(0.0f, 100.0f), ImVec2(200.0f, 500.0f));

	constexpr float centre_x = 100.0f;
	constexpr float top_band = 110.0f;
	constexpr float above = 50.0f;
	constexpr float bottom_band = 490.0f;
	constexpr float below = 600.0f;
	constexpr float middle = 300.0f;
	constexpr float left_of = -5.0f;
	constexpr float right_of = 205.0f;

	constexpr float carried = 0.25f;
};

#pragma endregion


TEST_SUITE("ComponentDrag")
{
	#pragma region Folding

	TEST_CASE("every header shows folded while a component is dragged, and each opens again after as it was, one the user had folded included")
	{
		Inspector inspector;

		CHECK(Inspector::Frame(false) == all_open);
		CHECK(Inspector::Frame(false, mesh) == mesh_folded);

		CHECK(Inspector::Frame(true) == all_folded);
		CHECK(Inspector::Frame(true) == all_folded);

		CHECK(Inspector::Frame(false) == mesh_folded);
		CHECK(Inspector::Frame(false) == mesh_folded);
	};

	TEST_CASE_FIXTURE(TwoObjects, "only a component of the GameObject shown is dragged to reorder it, so dragging anything else folds nothing")
	{
		CHECK(ComponentDrag::Dragged(*shown, own->GetGuid()) == own);
		CHECK(ComponentDrag::Dragged(*other, others->GetGuid()) == others);

		CHECK(ComponentDrag::Dragged(*shown, others->GetGuid()) == nullptr);
		CHECK(ComponentDrag::Dragged(*shown, other->GetGuid()) == nullptr);
		CHECK(ComponentDrag::Dragged(*shown, shown->GetGuid()) == nullptr);
		CHECK(ComponentDrag::Dragged(*shown, Loom::Guid{ }) == nullptr);
	};

	#pragma endregion

	#pragma region Scrolling

	TEST_CASE("held within the edge rows or past them it scrolls up or down, a row's worth by the time and the rate")
	{
		CHECK(ComponentDrag::Step(ImVec2(centre_x, top_band), inner, row, half_second, 0.0f).pixels == -half_second_pixels);
		CHECK(ComponentDrag::Step(ImVec2(centre_x, above), inner, row, half_second, 0.0f).pixels == -half_second_pixels);
		CHECK(ComponentDrag::Step(ImVec2(centre_x, bottom_band), inner, row, half_second, 0.0f).pixels == half_second_pixels);
		CHECK(ComponentDrag::Step(ImVec2(centre_x, below), inner, row, half_second, 0.0f).pixels == half_second_pixels);
	};

	TEST_CASE("held between the edge rows or beside the Inspector it does not scroll, and keeps what it carried")
	{
		for (const ImVec2& mouse : { ImVec2(centre_x, middle), ImVec2(left_of, top_band), ImVec2(right_of, bottom_band), ImVec2(left_of, middle) })
		{
			CAPTURE(mouse.x);
			CAPTURE(mouse.y);

			const ComponentDrag::Scroll scroll = ComponentDrag::Step(mouse, inner, row, half_second, carried);

			CHECK(scroll.pixels == 0.0f);
			CHECK(scroll.carried == carried);
		};
	};

	TEST_CASE("frames too fast for a whole pixel carry their steps on until they add up to one, both ways")
	{
		constexpr float quarter_pixel = 0.25f;
		constexpr int frames = 40;

		const float dt = quarter_pixel / (ComponentDrag::rows_per_second * row);

		for (const float y : { top_band, bottom_band })
		{
			CAPTURE(y);

			const float sign = y < middle ? -1.0f : 1.0f;
			const float step = dt * ComponentDrag::rows_per_second * row;

			const ComponentDrag::Scroll first = ComponentDrag::Step(ImVec2(centre_x, y), inner, row, dt, 0.0f);

			CHECK(first.pixels == 0.0f);
			CHECK(first.carried == doctest::Approx(sign * step));

			float scrolled = 0.0f;
			float left = 0.0f;

			for (int frame = 0; frame < frames; frame++)
			{
				const ComponentDrag::Scroll scroll = ComponentDrag::Step(ImVec2(centre_x, y), inner, row, dt, left);

				CHECK(std::abs(scroll.pixels) <= 1.0f);
				CHECK(std::abs(scroll.carried) < 1.0f);

				scrolled += scroll.pixels;
				left = scroll.carried;
			};

			CHECK(scrolled + left == doctest::Approx(sign * step * frames));
			CHECK(std::abs(scrolled) >= std::floor(step * frames) - 1.0f);
		};
	};

	#pragma endregion
};
