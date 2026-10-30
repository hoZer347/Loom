#include "doctest.h"

#include "ImGuiAutoscroll.h"

#include "imgui_internal.h"

#include <functional>

namespace
{
	constexpr float width = 800.0f;
	constexpr float height = 600.0f;
	constexpr float frame_time = 1.0f / 60.0f;

	constexpr float panel_side = 300.0f;
	constexpr float panel_centre = panel_side / 2.0f;
	constexpr float tall_content = 5000.0f;
	constexpr float wide_content = 5000.0f;

	constexpr int settle_frames = 3;
	constexpr int scroll_frames = 30;

	constexpr float inside_dead_zone = 6.0f;
	constexpr float just_past_dead_zone = 13.0f;
	constexpr float near_drag = 40.0f;
	constexpr float far_drag = 120.0f;

	// An ImGui context of the test's own with autoscroll on it, and a panel in
	// its top left that scrolls.
	struct Driven
	{
		Driven()
		{
			context = ImGui::CreateContext();
			ImGuiAutoscroll::Install();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(width, height);
			io.DeltaTime = frame_time;

			// Every event lands in the frame it was sent before.
			io.ConfigInputTrickleEventQueue = false;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);

			io.AddMousePosEvent(panel_centre, panel_centre);
		};

		~Driven()
		{
			ImGui::DestroyContext(context);
		};

		void Frame(const std::function<void()>& contents)
		{
			ImGui::NewFrame();

			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(panel_side, panel_side));

			if (ImGui::Begin("Panel", nullptr, panelFlags))
			{
				contents();
				scroll = ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY());
				middleClicked |= ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
				leftClicked |= ImGui::IsMouseClicked(ImGuiMouseButton_Left);
				escaped |= ImGui::IsKeyPressed(ImGuiKey_Escape);
			};

			ImGui::End();
			ImGui::EndFrame();
		};

		void Frames(int count, const std::function<void()>& contents)
		{
			for (int frame = 0; frame < count; frame++)
				Frame(contents);
		};

		void Tall()
		{
			Frames(settle_frames, []() { ImGui::Dummy(ImVec2(1.0f, tall_content)); });
		};

		void TallFrames(int count)
		{
			Frames(count, []() { ImGui::Dummy(ImVec2(1.0f, tall_content)); });
		};

		void MouseAt(float x, float y)
		{
			ImGui::GetIO().AddMousePosEvent(x, y);
		};

		void Button(ImGuiMouseButton button, bool down)
		{
			ImGui::GetIO().AddMouseButtonEvent(button, down);
		};

		ImGuiContext* context = nullptr;
		ImGuiWindowFlags panelFlags = ImGuiWindowFlags_None;

		ImVec2 scroll{ };
		bool middleClicked = false;
		bool leftClicked = false;
		bool escaped = false;
	};
};

TEST_SUITE("ImGuiAutoscroll")
{
	TEST_CASE_FIXTURE(Driven, "a held drag scrolls toward the pointer and letting go ends it")
	{
		Tall();

		Button(ImGuiMouseButton_Middle, true);
		TallFrames(1);
		CHECK(ImGuiAutoscroll::IsActive());
		CHECK_FALSE(middleClicked);

		MouseAt(panel_centre, panel_centre + near_drag);
		TallFrames(scroll_frames);
		CHECK(scroll.y > 0.0f);
		CHECK(ImGuiAutoscroll::Heading().y == 1.0f);
		CHECK(ImGuiAutoscroll::Heading().x == 0.0f);

		const float scrolled = scroll.y;

		MouseAt(panel_centre, panel_centre - far_drag);
		TallFrames(1);
		CHECK(ImGuiAutoscroll::Heading().y == -1.0f);

		TallFrames(scroll_frames);
		CHECK(scroll.y < scrolled);

		Button(ImGuiMouseButton_Middle, false);
		TallFrames(1);
		CHECK_FALSE(ImGuiAutoscroll::IsActive());
		CHECK_FALSE(ImGuiAutoscroll::IsSticky());

		const float stopped = scroll.y;

		TallFrames(scroll_frames);
		CHECK(scroll.y == stopped);
	};

	TEST_CASE("it scrolls faster the further the pointer gets")
	{
		const auto scrolled =
			[](float distance)
			{
				Driven driven;

				driven.Tall();
				driven.Button(ImGuiMouseButton_Middle, true);
				driven.TallFrames(1);
				driven.MouseAt(panel_centre, panel_centre + distance);
				driven.TallFrames(scroll_frames);

				return driven.scroll.y;
			};

		const float near_scroll = scrolled(near_drag);
		const float far_scroll = scrolled(far_drag);

		CHECK(near_scroll > 0.0f);
		CHECK(far_scroll > near_scroll * 2.0f);
	};

	TEST_CASE_FIXTURE(Driven, "nothing moves in the dead zone")
	{
		Tall();

		Button(ImGuiMouseButton_Middle, true);
		TallFrames(1);
		MouseAt(panel_centre + inside_dead_zone, panel_centre + inside_dead_zone);
		TallFrames(scroll_frames);

		CHECK(ImGuiAutoscroll::IsActive());
		CHECK(scroll.y == 0.0f);
		CHECK(ImGuiAutoscroll::Heading().x == 0.0f);
		CHECK(ImGuiAutoscroll::Heading().y == 0.0f);
	};

	TEST_CASE_FIXTURE(Driven, "a speed under a pixel a frame still adds up")
	{
		Tall();

		Button(ImGuiMouseButton_Middle, true);
		TallFrames(1);
		MouseAt(panel_centre, panel_centre + just_past_dead_zone);
		TallFrames(scroll_frames * 2);

		CHECK(scroll.y > 0.0f);
	};

	TEST_CASE_FIXTURE(Driven, "a click stays on until the next click, which nothing else hears")
	{
		Tall();

		Button(ImGuiMouseButton_Middle, true);
		TallFrames(1);
		Button(ImGuiMouseButton_Middle, false);
		TallFrames(1);
		CHECK(ImGuiAutoscroll::IsSticky());

		MouseAt(panel_centre, panel_centre + near_drag);
		TallFrames(scroll_frames);
		CHECK(scroll.y > 0.0f);

		Button(ImGuiMouseButton_Left, true);
		TallFrames(1);
		CHECK_FALSE(ImGuiAutoscroll::IsActive());
		CHECK_FALSE(leftClicked);

		Button(ImGuiMouseButton_Left, false);
		TallFrames(1);
		CHECK_FALSE(ImGuiAutoscroll::IsActive());
	};

	TEST_CASE_FIXTURE(Driven, "a middle click ends what a middle click began, and starts nothing")
	{
		Tall();

		Button(ImGuiMouseButton_Middle, true);
		TallFrames(1);
		Button(ImGuiMouseButton_Middle, false);
		TallFrames(1);

		Button(ImGuiMouseButton_Middle, true);
		TallFrames(1);
		CHECK_FALSE(ImGuiAutoscroll::IsActive());

		Button(ImGuiMouseButton_Middle, false);
		TallFrames(1);
		CHECK_FALSE(ImGuiAutoscroll::IsActive());
		CHECK_FALSE(middleClicked);
	};

	TEST_CASE_FIXTURE(Driven, "Escape ends it and is not heard by anything else")
	{
		Tall();

		Button(ImGuiMouseButton_Middle, true);
		TallFrames(1);
		Button(ImGuiMouseButton_Middle, false);
		TallFrames(1);
		CHECK(ImGuiAutoscroll::IsSticky());

		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
		TallFrames(1);
		CHECK_FALSE(ImGuiAutoscroll::IsActive());
		CHECK_FALSE(escaped);

		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
		TallFrames(1);
		CHECK_FALSE(ImGuiAutoscroll::IsActive());
	};

	TEST_CASE_FIXTURE(Driven, "an axis that does not overflow does not move or point")
	{
		Tall();

		Button(ImGuiMouseButton_Middle, true);
		TallFrames(1);
		MouseAt(panel_centre + far_drag, panel_centre + inside_dead_zone);
		TallFrames(scroll_frames);

		CHECK(scroll.x == 0.0f);
		CHECK(scroll.y == 0.0f);
		CHECK(ImGuiAutoscroll::Heading().x == 0.0f);

		// Sideways alone has left no dead zone behind, so this is still a click.
		Button(ImGuiMouseButton_Middle, false);
		TallFrames(1);
		CHECK(ImGuiAutoscroll::IsSticky());
	};

	TEST_CASE_FIXTURE(Driven, "both axes move when both overflow")
	{
		panelFlags = ImGuiWindowFlags_HorizontalScrollbar;

		const auto contents = []() { ImGui::Dummy(ImVec2(wide_content, tall_content)); };

		Frames(settle_frames, contents);

		Button(ImGuiMouseButton_Middle, true);
		Frame(contents);
		MouseAt(panel_centre + near_drag, panel_centre + near_drag);
		Frames(scroll_frames, contents);

		CHECK(scroll.x > 0.0f);
		CHECK(scroll.y > 0.0f);
		CHECK(ImGuiAutoscroll::Heading().x == 1.0f);
		CHECK(ImGuiAutoscroll::Heading().y == 1.0f);
	};

	TEST_CASE_FIXTURE(Driven, "content wider than a window with no sideways scrollbar stays put")
	{
		const auto contents = []() { ImGui::Dummy(ImVec2(wide_content, tall_content)); };

		Frames(settle_frames, contents);

		Button(ImGuiMouseButton_Middle, true);
		Frame(contents);
		MouseAt(panel_centre + near_drag, panel_centre + near_drag);
		Frames(scroll_frames, contents);

		CHECK(scroll.x == 0.0f);
		CHECK(scroll.y > 0.0f);
	};

	TEST_CASE_FIXTURE(Driven, "a window with nothing to scroll leaves the press alone")
	{
		const auto contents = []() { ImGui::Dummy(ImVec2(1.0f, 1.0f)); };

		Frames(settle_frames, contents);

		Button(ImGuiMouseButton_Middle, true);
		Frame(contents);

		CHECK_FALSE(ImGuiAutoscroll::IsActive());
		CHECK(middleClicked);
	};

	TEST_CASE_FIXTURE(Driven, "a window that turned the wheel off leaves the press alone")
	{
		panelFlags = ImGuiWindowFlags_NoScrollWithMouse;

		Tall();

		Button(ImGuiMouseButton_Middle, true);
		TallFrames(1);

		CHECK_FALSE(ImGuiAutoscroll::IsActive());
		CHECK(middleClicked);
	};

	TEST_CASE_FIXTURE(Driven, "a press off every window leaves it alone")
	{
		Tall();

		MouseAt(width - 1.0f, height - 1.0f);
		TallFrames(1);
		Button(ImGuiMouseButton_Middle, true);
		TallFrames(1);

		CHECK_FALSE(ImGuiAutoscroll::IsActive());
	};

	TEST_CASE_FIXTURE(Driven, "a press on the title bar leaves it alone")
	{
		Tall();

		constexpr float title_row = 5.0f;

		MouseAt(panel_centre, title_row);
		TallFrames(1);
		Button(ImGuiMouseButton_Middle, true);
		TallFrames(1);

		CHECK_FALSE(ImGuiAutoscroll::IsActive());
	};

	TEST_CASE_FIXTURE(Driven, "whoever keeps the middle button keeps the press")
	{
		const auto contents =
			[]()
			{
				ImGui::Dummy(ImVec2(1.0f, tall_content));
				ImGuiAutoscroll::KeepMiddleMouse();
			};

		Frames(settle_frames, contents);

		Button(ImGuiMouseButton_Middle, true);
		Frame(contents);

		CHECK_FALSE(ImGuiAutoscroll::IsActive());
		CHECK(middleClicked);
	};

	TEST_CASE_FIXTURE(Driven, "a child with nothing to scroll hands the press to its parent")
	{
		const auto contents =
			[]()
			{
				ImGui::BeginChild("Child", ImVec2(panel_side, panel_side));
				ImGui::Dummy(ImVec2(1.0f, 1.0f));
				ImGui::EndChild();
				ImGui::Dummy(ImVec2(1.0f, tall_content));
			};

		Frames(settle_frames, contents);

		Button(ImGuiMouseButton_Middle, true);
		Frame(contents);
		CHECK(ImGuiAutoscroll::IsActive());

		MouseAt(panel_centre, panel_centre + near_drag);
		Frames(scroll_frames, contents);
		CHECK(scroll.y > 0.0f);
	};

	TEST_CASE_FIXTURE(Driven, "a child that scrolls takes the press over its parent")
	{
		float child_scroll = 0.0f;

		const auto contents =
			[&child_scroll]()
			{
				ImGui::BeginChild("Child", ImVec2(panel_side, panel_side));
				ImGui::Dummy(ImVec2(1.0f, tall_content));
				child_scroll = ImGui::GetScrollY();
				ImGui::EndChild();
				ImGui::Dummy(ImVec2(1.0f, tall_content));
			};

		Frames(settle_frames, contents);

		Button(ImGuiMouseButton_Middle, true);
		Frame(contents);
		MouseAt(panel_centre, panel_centre + near_drag);
		Frames(scroll_frames, contents);

		CHECK(child_scroll > 0.0f);
		CHECK(scroll.y == 0.0f);
	};

	TEST_CASE_FIXTURE(Driven, "a closable tab keeps its middle click")
	{
		bool open = true;

		const auto contents =
			[&open]()
			{
				if (ImGui::BeginTabBar("Tabs"))
				{
					if (ImGui::BeginTabItem("Tab", &open))
						ImGui::EndTabItem();

					ImGui::EndTabBar();
				};

				ImGui::Dummy(ImVec2(1.0f, tall_content));
			};

		Frames(settle_frames, contents);

		const ImVec2 corner = ImGui::FindWindowByName("Panel")->InnerRect.Min;
		const ImVec2 padding = ImGui::GetStyle().WindowPadding;

		constexpr float into_tab = 8.0f;

		MouseAt(corner.x + padding.x + into_tab, corner.y + padding.y + into_tab);
		Frames(settle_frames, contents);

		Button(ImGuiMouseButton_Middle, true);
		Frame(contents);
		Button(ImGuiMouseButton_Middle, false);
		Frames(settle_frames, contents);

		CHECK_FALSE(ImGuiAutoscroll::IsActive());
		CHECK_FALSE(open);
	};

	TEST_CASE_FIXTURE(Driven, "a press with another button down is left alone")
	{
		Tall();

		Button(ImGuiMouseButton_Right, true);
		TallFrames(1);
		Button(ImGuiMouseButton_Middle, true);
		TallFrames(1);

		CHECK_FALSE(ImGuiAutoscroll::IsActive());
	};

	TEST_CASE_FIXTURE(Driven, "it ends with the window it was scrolling")
	{
		Tall();

		Button(ImGuiMouseButton_Middle, true);
		TallFrames(1);
		Button(ImGuiMouseButton_Middle, false);
		TallFrames(1);
		CHECK(ImGuiAutoscroll::IsSticky());

		ImGui::NewFrame();
		ImGui::EndFrame();
		ImGui::NewFrame();
		ImGui::EndFrame();

		CHECK_FALSE(ImGuiAutoscroll::IsActive());
	};

	TEST_CASE_FIXTURE(Driven, "it ends when the app loses focus")
	{
		Tall();

		Button(ImGuiMouseButton_Middle, true);
		TallFrames(1);
		Button(ImGuiMouseButton_Middle, false);
		TallFrames(1);

		ImGui::GetIO().AddFocusEvent(false);
		TallFrames(1);

		CHECK_FALSE(ImGuiAutoscroll::IsActive());
	};

	TEST_CASE_FIXTURE(Driven, "the pointer hides for a marker and a drawn cursor while it runs")
	{
		Tall();

		const auto drawn = []() { return ImGui::GetForegroundDrawList(ImGui::GetMainViewport())->VtxBuffer.Size; };

		CHECK(drawn() == 0);

		Button(ImGuiMouseButton_Middle, true);
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(panel_side, panel_side));
		ImGui::Begin("Panel");
		ImGui::Dummy(ImVec2(1.0f, tall_content));
		ImGui::End();

		CHECK(ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow);

		ImGui::EndFrame();

		CHECK(ImGui::GetMouseCursor() == ImGuiMouseCursor_None);
		CHECK(drawn() > 0);

		Button(ImGuiMouseButton_Middle, false);
		TallFrames(1);
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
		TallFrames(1);

		CHECK(ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow);
	};

	TEST_CASE_FIXTURE(Driven, "what the pointer passes over does not light up while it runs")
	{
		bool hovered = false;

		const auto contents =
			[&hovered]()
			{
				ImGui::Button("Row", ImVec2(panel_side, tall_content));
				hovered = ImGui::IsItemHovered();
			};

		Frames(settle_frames, contents);
		CHECK(hovered);

		Button(ImGuiMouseButton_Middle, true);
		Frames(settle_frames, contents);
		CHECK_FALSE(hovered);

		Button(ImGuiMouseButton_Middle, false);
		Frames(settle_frames, contents);
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
		Frames(settle_frames, contents);
		CHECK(hovered);
	};

	TEST_CASE("a context without it answers that nothing is running")
	{
		ImGuiContext* const previous = ImGui::GetCurrentContext();

		ImGuiContext* const context = ImGui::CreateContext();

		ImGuiAutoscroll::KeepMiddleMouse();

		CHECK_FALSE(ImGuiAutoscroll::IsActive());
		CHECK_FALSE(ImGuiAutoscroll::IsSticky());

		ImGui::DestroyContext(context);
		ImGui::SetCurrentContext(previous);
	};
};
