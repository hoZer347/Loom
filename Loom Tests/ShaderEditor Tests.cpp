#include "doctest.h"

// The shader editor checks with glslang, which the web build does not have.
#ifndef __EMSCRIPTEN__

#include "EditorIcons.h"
#include "ShaderEditor.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>

#pragma region Helpers

namespace
{
	constexpr float width = 900.0f;
	constexpr float height = 600.0f;

	// Longer than the autocomplete popup and the check each wait after a
	// keystroke, which they time by the clock rather than by frames.
	constexpr std::chrono::milliseconds settle{ 500 };

	// Enough for the panel and then the text in it to take focus.
	constexpr int opening_frames = 4;

	// The second line is empty, for typing on in the vertex stage.
	const char* const shader =
		"// ===VERTEX===\n"
		"\n"
		"void main() { gl_Position = vec4(0.0); }\n"
		"// ===FRAGMENT===\n"
		"out vec4 FragColor;\n"
		"void main() { FragColor = vec4(1.0); }\n";

	// An ImGui context of the test's own, with no window and no renderer, the
	// Shaders panel filling the display and a shader that compiles open in it.
	struct Driven
	{
		explicit Driven(const char* text = shader) :
			path(std::filesystem::temp_directory_path() / "ShaderEditor Tests.shader")
		{
			constexpr float frame_time = 1.0f / 60.0f;

			context = ImGui::CreateContext();

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

			std::ofstream(path, std::ios::binary) << text;

			editor.Open(path.string());

			for (int i = 0; i < opening_frames; i++)
				Frame();

			Press(ImGuiKey_DownArrow);
		};

		~Driven()
		{
			ImGui::DestroyContext(context);

			std::error_code code;
			std::filesystem::remove(path, code);
		};

		void Frame()
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(width, height));
			editor.Draw(&open);
			ImGui::EndFrame();
		};

		void Type(const char* text)
		{
			ImGui::GetIO().AddInputCharactersUTF8(text);
			Frame();
		};

		void Press(ImGuiKey key, bool control = false)
		{
			ImGuiIO& io = ImGui::GetIO();

			io.AddKeyEvent(ImGuiMod_Ctrl, control);
			io.AddKeyEvent(key, true);
			Frame();

			io.AddKeyEvent(key, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false);
			Frame();
		};

		void Settle()
		{
			std::this_thread::sleep_for(settle);
			Frame();
			Frame();
		};

		std::string Saved() const
		{
			std::ifstream file(path, std::ios::binary);

			return std::string(std::istreambuf_iterator<char>(file), { });
		};

		// Rests the mouse at points along the given line, from its start, and
		// says whether a tooltip came up at any of them.
		bool TooltipAlong(int line)
		{
			constexpr float half = 0.5f;
			constexpr int stride = 3;
			constexpr int glyphs = 30;

			// Longer than ImGui waits on a still mouse before a tooltip.
			constexpr int resting_frames = 30;

			const ImGuiWindow* text = Window("##text");

			REQUIRE(text != nullptr);

			const float glyph = ImGui::CalcTextSize("x").x;
			const float y = text->InnerRect.Min.y + (line + half) * ImGui::GetFontSize();

			for (int column = 0; column < glyphs; column += stride)
			{
				ImGui::GetIO().AddMousePosEvent(text->InnerRect.Min.x + (column + half) * glyph, y);

				for (int i = 0; i < resting_frames; i++)
				{
					Frame();

					if (Window("##Tooltip_") != nullptr)
						return true;
				};
			};

			return false;
		};

		// The first window drawn this frame with that in its name.
		static const ImGuiWindow* Window(const char* part)
		{
			for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
				if (window->Active && std::strstr(window->Name, part) != nullptr)
					return window;

			return nullptr;
		};

		// Whether the tab shows its document as unsaved, once a frame has drawn
		// it since the last keystroke.
		bool Unsaved()
		{
			Frame();

			ImGuiContext& g = *ImGui::GetCurrentContext();

			for (int i = 0; i < g.TabBars.GetMapSize(); i++)
				if (const ImGuiTabBar* bar = g.TabBars.TryGetMapData(i))
					for (const ImGuiTabItem& tab : bar->Tabs)
						if (tab.Flags & ImGuiTabItemFlags_UnsavedDocument)
							return true;

			return false;
		};

		// The name of the tab in front, without its ### part.
		static std::string SelectedTab()
		{
			ImGuiContext& g = *ImGui::GetCurrentContext();

			for (int i = 0; i < g.TabBars.GetMapSize(); i++)
				if (ImGuiTabBar* bar = g.TabBars.TryGetMapData(i))
					if (ImGuiTabItem* tab = ImGui::TabBarFindTabByID(bar, bar->SelectedTabId))
						return ImGui::TabBarGetTabName(bar, tab);

			return "";
		};

		// Whether glslang's messages are listed under the text this frame.
		static bool Listed()
		{
			for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
				if (window->Active && std::strstr(window->Name, "##messages") != nullptr)
					return true;

			return false;
		};

		const std::filesystem::path path;
		ImGuiContext* context = nullptr;
		Loom::ShaderEditor editor;
		bool open = true;
	};
};

#pragma endregion

TEST_SUITE("ShaderEditor")
{
	#pragma region Typing and Saving

	TEST_CASE("typing an engine name offers it, and Tab and Ctrl+S put it in the file")
	{
		Driven driven;

		driven.Type("LoomLi");
		driven.Settle();
		driven.Press(ImGuiKey_Tab);
		driven.Press(ImGuiKey_S, true);

		CHECK(driven.Saved().starts_with("// ===VERTEX===\nLoomLight\n"));
	};

	TEST_CASE("a mistake is listed under the text a moment after it is typed")
	{
		Driven driven;

		CHECK_FALSE(Driven::Listed());

		driven.Type("x");
		driven.Settle();

		CHECK(Driven::Listed());
	};

	TEST_CASE("an edit stays out of the file until Ctrl+S")
	{
		Driven driven;

		driven.Type("x");
		driven.Settle();

		CHECK(driven.Saved() == shader);
	};

	TEST_CASE("saving everything writes an edited shader")
	{
		Driven driven;

		driven.Type("x");
		driven.Settle();

		CHECK(driven.editor.SaveAll());
		CHECK(driven.Saved() != shader);
		CHECK(driven.Saved().find('x') != std::string::npos);
	};

	TEST_CASE("an edited shader is listed as unsaved by name until it is saved")
	{
		Driven driven;
		const std::vector<std::string> name{ std::filesystem::path(driven.path).filename().string() };

		CHECK(driven.editor.UnsavedFiles().empty());

		driven.Type("x");
		driven.Settle();
		CHECK(driven.editor.UnsavedFiles() == name);

		REQUIRE(driven.editor.SaveAll());
		CHECK(driven.editor.UnsavedFiles().empty());
	};

	TEST_CASE("saving everything leaves a shader with no edits alone")
	{
		constexpr const char* changed_outside = "// written by something else\n";

		Driven driven;

		std::ofstream(driven.path, std::ios::binary) << changed_outside;

		CHECK(driven.editor.SaveAll());
		CHECK(driven.Saved() == changed_outside);
	};

	TEST_CASE("opening a shader that is already open adds no tab")
	{
		Driven driven;

		driven.editor.Open(driven.path.string());
		driven.Frame();

		int tabs = 0;

		for (int i = 0; i < ImGui::GetCurrentContext()->TabBars.GetMapSize(); i++)
			if (const ImGuiTabBar* bar = ImGui::GetCurrentContext()->TabBars.TryGetMapData(i))
				tabs += bar->Tabs.Size;

		CHECK(tabs == 1);
	};

	TEST_CASE("an edit made after undoing back past a save still counts as unsaved")
	{
		Driven driven;

		driven.Type("x");
		driven.Press(ImGuiKey_S, true);
		driven.Press(ImGuiKey_Z, true);
		driven.Type("y");

		CHECK(driven.Unsaved());
	};

	TEST_CASE("undoing back to what was saved counts as saved")
	{
		Driven driven;

		driven.Type("x");
		CHECK(driven.Unsaved());

		driven.Press(ImGuiKey_Z, true);
		CHECK_FALSE(driven.Unsaved());
	};

	TEST_CASE("undoing back to what was saved counts as saved in a file with Windows line endings")
	{
		constexpr const char* crlf = "#version 330 core\r\nvoid main() { }\r\n";

		Driven driven(crlf);

		driven.Type("x");
		CHECK(driven.Unsaved());

		driven.Press(ImGuiKey_Z, true);
		CHECK_FALSE(driven.Unsaved());
		CHECK(driven.Saved() == crlf);
	};

	TEST_CASE("Ctrl+Z and Ctrl+Y step the text with the panel rather than the text holding the keyboard")
	{
		Driven driven;

		driven.Type("x");
		ImGui::FocusWindow(ImGui::FindWindowByName(Loom::EditorPanels::Shaders));
		driven.Frame();

		driven.Press(ImGuiKey_Z, true);
		CHECK_FALSE(driven.Unsaved());

		driven.Press(ImGuiKey_Y, true);
		CHECK(driven.Unsaved());
		CHECK(driven.Saved() == shader);

		driven.Press(ImGuiKey_S, true);
		CHECK(driven.Saved().find('x') != std::string::npos);
	};

	#pragma endregion

	#pragma region Messages and Explanations

	TEST_CASE("a message underlines the name it quotes, not the same letters inside another")
	{
		using Loom::ShaderEditor;

		CHECK(ShaderEditor::MessageSpan("\tcolor = col;", "'col' : undeclared identifier") == std::pair<size_t, size_t>{ 9, 12 });
		CHECK(ShaderEditor::MessageSpan("col", "'col' : undeclared identifier") == std::pair<size_t, size_t>{ 0, 3 });
		CHECK(ShaderEditor::MessageSpan("\tcolor = 1.0;", "'col' : undeclared identifier") == std::pair<size_t, size_t>{ 1, 13 });
		CHECK(ShaderEditor::MessageSpan("  x = y;", "'' : syntax error") == std::pair<size_t, size_t>{ 2, 8 });
	};

	TEST_CASE("resting the mouse on an engine name explains it")
	{
		Driven driven("// ===VERTEX===\nuniform mat4 u_model;\n");

		CHECK(driven.TooltipAlong(1));
	};

	TEST_CASE("a name of the shader's own gets no explanation")
	{
		Driven driven("// ===VERTEX===\nuniform mat4 u_mine;\n");

		CHECK_FALSE(driven.TooltipAlong(1));
	};

	TEST_CASE("a kernel of any size is explained by its kind")
	{
		Driven sized("// ===PIXEL PASS===\nPositionKernel_17x5 around;\n");

		CHECK(sized.TooltipAlong(1));
	};

	TEST_CASE("a kernel's size run straight onto its kind gets no explanation")
	{
		Driven unsized("// ===PIXEL PASS===\nPositionKernel17x5 around;\n");

		CHECK_FALSE(unsized.TooltipAlong(1));
	};

	#pragma endregion

	#pragma region Moving

	TEST_CASE("a shader moved while its tab is in front stays in front, and Ctrl+S writes where it went")
	{
		Driven driven;

		const std::filesystem::path opened = driven.path.parent_path() / "ShaderEditor Tests Opened.shader";
		const std::filesystem::path moved = driven.path.parent_path() / "ShaderEditor Tests Moved.shader";

		std::ofstream(opened, std::ios::binary) << shader;

		driven.editor.Open(opened.string());

		for (int i = 0; i < opening_frames; i++)
			driven.Frame();

		std::filesystem::rename(opened, moved);
		driven.editor.Relocate(opened.lexically_normal().string(), moved.string());
		driven.Frame();

		CHECK(Driven::SelectedTab().find(moved.filename().string()) != std::string::npos);

		driven.Type("x");
		driven.Press(ImGuiKey_S, true);

		std::ifstream file(moved, std::ios::binary);
		const std::string saved((std::istreambuf_iterator<char>(file)), { });
		file.close();

		CHECK(saved.find('x') != std::string::npos);
		CHECK_FALSE(std::filesystem::exists(opened));

		std::filesystem::remove(moved);
	};

	#pragma endregion
};

#endif
