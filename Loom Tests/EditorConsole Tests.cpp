#include "doctest.h"

#include "EditorConsole.h"
#include "EditorLog.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
	std::string clipboard;

	// An ImGui context of the test's own, with no window and no renderer, a
	// console filling the display, and three lines in the log.
	struct Driven
	{
		Driven()
		{
			constexpr float frame_time = 1.0f / 60.0f;

			context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(width, height);
			io.DeltaTime = frame_time;
			io.SetClipboardTextFn = [](void*, const char* text) { clipboard = text; };

			// Every event lands in the frame it was sent before.
			io.ConfigInputTrickleEventQueue = false;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);

			clipboard.clear();

			Loom::EditorLog::Get().Install();
			Loom::EditorLog::Get().Clear();

			for (const char* line : lines)
				std::cout << line << std::endl;

			Frame();
		};

		~Driven()
		{
			Loom::EditorLog::Get().Clear();
			Loom::EditorLog::Get().Uninstall();

			ImGui::DestroyContext(context);
		};

		void Frame()
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(width, height));
			console.Draw(&open);
			ImGui::EndFrame();
		};

		// The scrolling child that holds the console's lines.
		static const ImGuiWindow& Lines()
		{
			const ImGuiWindow* found = nullptr;

			for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
				if (window->ParentWindow && std::strcmp(window->ParentWindow->Name, "Console") == 0)
					found = window;

			REQUIRE(found);

			return *found;
		};

		// The middle of the boundary before column on the given line.
		static ImVec2 At(int line, int column)
		{
			const ImGuiWindow& text = Lines();

			return ImVec2(
				text.ContentRegionRect.Min.x + column * ImGui::CalcTextSize("x").x,
				text.ContentRegionRect.Min.y
					+ line * ImGui::GetTextLineHeightWithSpacing()
					+ ImGui::GetTextLineHeight() * half);
		};

		// The middle of the given row of the open right-click menu.
		static ImVec2 MenuRow(int row)
		{
			constexpr float indent = 10.0f;

			const ImVector<ImGuiPopupData>& popups = ImGui::GetCurrentContext()->OpenPopupStack;

			REQUIRE(!popups.empty());

			const ImGuiWindow* menu = popups.back().Window;

			REQUIRE(menu);

			return ImVec2(
				menu->ContentRegionRect.Min.x + indent,
				menu->ContentRegionRect.Min.y
					+ row * ImGui::GetTextLineHeightWithSpacing()
					+ ImGui::GetTextLineHeight() * half);
		};

		// The filter box fills the toolbar from the checkbox to the right edge.
		static ImVec2 Filter()
		{
			constexpr float inset = 20.0f;

			const ImGuiWindow* panel = ImGui::FindWindowByName("Console");

			REQUIRE(panel);

			return ImVec2(
				panel->ContentRegionRect.Max.x - inset,
				panel->ContentRegionRect.Min.y + ImGui::GetFrameHeight() * half);
		};

		static ImVec2 TitleBar()
		{
			return ImVec2(width * half, ImGui::GetFrameHeight() * half);
		};

		// Left and right of everything drawn in the selection colour.
		static std::pair<float, float> Highlighted()
		{
			const ImDrawList& list = *Lines().DrawList;
			const ImU32 colour = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);

			float left = FLT_MAX;
			float right = -FLT_MAX;

			for (const ImDrawVert& vertex : list.VtxBuffer)
				if (vertex.col == colour)
				{
					left = std::min(left, vertex.pos.x);
					right = std::max(right, vertex.pos.x);
				};

			return { left, right };
		};

		void Press(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			Frame();
		};

		void Release(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);
			Frame();
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Frame();
		};

		void Drag(ImVec2 from, ImVec2 to)
		{
			Press(from);
			Release(to);
		};

		void Click(ImVec2 at)
		{
			Drag(at, at);
		};

		void RightClick(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, true);
			Frame();
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, false);
			Frame();

			// A new menu spends its first frame hidden, measuring itself.
			Frame();
		};

		void Type(const char* text)
		{
			ImGui::GetIO().AddInputCharactersUTF8(text);
			Frame();
		};

		void Chord(ImGuiKey key)
		{
			ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
			ImGui::GetIO().AddKeyEvent(key, true);
			Frame();
			ImGui::GetIO().AddKeyEvent(key, false);
			ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
			Frame();
		};

		static constexpr float half = 0.5f;
		static constexpr float width = 800.0f;
		static constexpr float height = 600.0f;

		const std::vector<const char*> lines{ "first line", "second line", "third line" };

		Loom::EditorConsole console;
		bool open = true;
		ImGuiContext* context = nullptr;
	};
};


TEST_SUITE("EditorConsole")
{
	TEST_CASE_FIXTURE(Driven, "dragging across lines copies from the press to the release")
	{
		Drag(At(0, 6), At(2, 5));
		Chord(ImGuiKey_C);

		CHECK(clipboard == "line\nsecond line\nthird");
	};

	TEST_CASE_FIXTURE(Driven, "dragging backwards copies the same text")
	{
		Drag(At(1, 11), At(1, 7));
		Chord(ImGuiKey_C);

		CHECK(clipboard == "line");
	};

	TEST_CASE_FIXTURE(Driven, "dragging past the last line selects to its end")
	{
		Drag(At(1, 0), ImVec2(At(2, 0).x, height - 1.0f));
		Chord(ImGuiKey_C);

		CHECK(clipboard == "second line\nthird line");
	};

	TEST_CASE_FIXTURE(Driven, "a double-click selects the whole line")
	{
		const ImVec2 at = At(1, 3);

		Press(at);
		Release(at);
		Press(at);
		Release(at);
		Chord(ImGuiKey_C);

		CHECK(clipboard == "second line");
	};

	TEST_CASE_FIXTURE(Driven, "Ctrl+A copies every line")
	{
		Drag(At(0, 0), At(0, 0));
		Chord(ImGuiKey_A);
		Chord(ImGuiKey_C);

		CHECK(clipboard == "first line\nsecond line\nthird line");
	};

	TEST_CASE_FIXTURE(Driven, "a click without a drag clears the selection")
	{
		Drag(At(0, 0), At(2, 3));
		Drag(At(1, 2), At(1, 2));
		Chord(ImGuiKey_C);

		CHECK(clipboard.empty());
	};

	TEST_CASE_FIXTURE(Driven, "clearing the log drops the selection rather than moving it onto new lines")
	{
		Drag(At(0, 0), At(0, 5));
		Loom::EditorLog::Get().Clear();
		std::cout << "fresh line" << std::endl;
		Frame();
		Chord(ImGuiKey_C);

		CHECK(clipboard.empty());
	};

	TEST_CASE_FIXTURE(Driven, "the selected columns are drawn highlighted")
	{
		Drag(At(1, 2), At(1, 6));

		const auto [left, right] = Highlighted();

		CHECK(left == doctest::Approx(At(1, 2).x));
		CHECK(right == doctest::Approx(At(1, 6).x));
	};

	TEST_CASE_FIXTURE(Driven, "a shift-click extends the selection")
	{
		Click(At(0, 2));

		ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, true);
		Click(At(1, 6));
		ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, false);

		Chord(ImGuiKey_C);

		CHECK(clipboard == "rst line\nsecond");
	};

	TEST_CASE_FIXTURE(Driven, "the right-click menu selects all and copies")
	{
		constexpr int copy_row = 0;
		constexpr int select_all_row = 1;

		RightClick(At(1, 0));
		Click(MenuRow(select_all_row));
		RightClick(At(1, 0));
		Click(MenuRow(copy_row));

		// The menu only asks for the copy, which happens on the next frame.
		Frame();

		CHECK(clipboard == "first line\nsecond line\nthird line");
	};

	TEST_CASE_FIXTURE(Driven, "copying skips lines the filter hides")
	{
		Click(Filter());
		Type("ir");
		Drag(At(0, 0), At(1, 5));
		Chord(ImGuiKey_C);

		CHECK(clipboard == "first line\nthird");
	};

	TEST_CASE_FIXTURE(Driven, "Ctrl+C still copies after the panel itself takes focus")
	{
		Drag(At(0, 0), At(0, 5));
		Click(TitleBar());
		Chord(ImGuiKey_C);

		CHECK(clipboard == "first");
	};
};
