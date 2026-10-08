#include "doctest.h"

#include "EditorConsole.h"
#include "EditorLog.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstring>
#include <iostream>
#include <string>
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

		// The middle of the boundary before column on the given line.
		ImVec2 At(int line, int column)
		{
			constexpr float half = 0.5f;

			const ImGuiWindow* text = nullptr;

			for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
				if (window->ParentWindow && std::strcmp(window->ParentWindow->Name, "Console") == 0)
					text = window;

			REQUIRE(text);

			return ImVec2(
				text->ContentRegionRect.Min.x + column * ImGui::CalcTextSize("x").x,
				text->ContentRegionRect.Min.y
					+ line * ImGui::GetTextLineHeightWithSpacing()
					+ ImGui::GetTextLineHeight() * half);
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

		void Chord(ImGuiKey key)
		{
			ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
			ImGui::GetIO().AddKeyEvent(key, true);
			Frame();
			ImGui::GetIO().AddKeyEvent(key, false);
			ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
			Frame();
		};

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
};
