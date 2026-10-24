#include "doctest.h"

#include "Test Support.h"

#include "EditorConsole.h"
#include "EditorIcons.h"
#include "EditorLog.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
	std::string clipboard;

	// A project folder of the test's own, holding the files its lines name,
	// that the log names files from while it lasts.
	struct Project final
	{
		Project()
		{
			std::filesystem::remove_all(folder);

			for (const char* file : files)
			{
				const std::filesystem::path path = folder / file;

				std::filesystem::create_directories(path.parent_path());
				std::ofstream(path) << file;
			};

			Loom::EditorLog::Get().Install();
			Loom::EditorLog::Get().Clear();
			Loom::EditorLog::Get().SetProject(folder.string());
		};

		~Project()
		{
			Loom::EditorLog::Get().SetProject("");
			Loom::EditorLog::Get().Clear();
			Loom::EditorLog::Get().Uninstall();

			std::filesystem::remove_all(folder);
		};

		// A file in the folder as Windows names it.
		std::string Path(const char* file) const
		{
			return (folder / file).make_preferred().string();
		};

		static std::vector<Loom::EditorLog::Line> Lines()
		{
			std::vector<Loom::EditorLog::Line> lines;
			Loom::EditorLog::Get().ForEach([&](size_t, const Loom::EditorLog::Line& line) { lines.push_back(line); });

			return lines;
		};

		// The text of a line that a link covers.
		static std::string Linked(const Loom::EditorLog::Line& line, const Loom::EditorLog::Link& link)
		{
			return line.text.substr(link.start, link.length);
		};

		const std::filesystem::path folder = std::filesystem::temp_directory_path() / "Loom EditorLog Tests";
		const std::vector<const char*> files{ "Scripts/Laser.hpp", "Assets/Overfull.loomsprite", "Assets/Some Models/Broken.ply" };
	};

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
				std::cerr << line << std::endl;

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
			console.Draw(&open, [this](const Loom::EditorLog::Link& link) { followed.push_back(link); });
			ImGui::EndFrame();
		};

		// The scrolling child that holds the console's lines.
		static const ImGuiWindow& Lines()
		{
			const ImGuiWindow* found = nullptr;

			for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
				if (window->ParentWindow && window->ParentWindow->ID == ImHashStr(Loom::EditorPanels::Console))
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

			const ImGuiWindow* panel = ImGui::FindWindowByName(Loom::EditorPanels::Console);

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

		// The log passes its lines on to here rather than the test's output.
		// Not std::cout, which doctest reports on.
		const LoomTests::Captured passedOn{ std::cerr };
		Loom::EditorConsole console;
		std::vector<Loom::EditorLog::Link> followed;
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

	TEST_CASE_FIXTURE(Driven, "a double-click below the text selects the last line, however long the first")
	{
		Loom::EditorLog::Get().Clear();
		std::cout << "a first line far longer than the last" << std::endl;
		std::cout << "last" << std::endl;
		Frame();

		const ImVec2 below = At(2, 0);

		Press(below);
		Release(below);
		Press(below);
		Release(below);

		const auto [left, right] = Highlighted();

		CHECK(left == doctest::Approx(At(1, 0).x));
		CHECK(right == doctest::Approx(At(1, 4).x));
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
		std::cerr << "fresh line" << std::endl;
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

	TEST_CASE_FIXTURE(Driven, "a click on a link follows it to its file, line and column")
	{
		const Project project;
		std::cerr << project.Path("Scripts/Laser.hpp") << "(111,7): error C2059" << std::endl;
		Frame();

		Click(At(0, 3));

		REQUIRE(followed.size() == 1);
		CHECK(followed[0].path == project.Path("Scripts/Laser.hpp"));
		CHECK(followed[0].line == 111);
		CHECK(followed[0].column == 7);
	};

	TEST_CASE_FIXTURE(Driven, "a click beside a link, a drag from one and a shift-click on one select rather than follow")
	{
		constexpr int before_link = 2;
		constexpr int in_link = 12;
		constexpr int further_in_link = 20;
		constexpr int extended_to = 25;

		const Project project;
		std::cerr << "In " << project.Path("Assets/Overfull.loomsprite") << " again" << std::endl;
		Frame();

		Click(At(0, before_link));
		Drag(At(0, in_link), At(0, further_in_link));

		ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, true);
		Click(At(0, extended_to));
		ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, false);

		Chord(ImGuiKey_C);

		CHECK(followed.empty());
		CHECK(clipboard == "erfull.loomsp");
	};
};

namespace
{
	// A stream nobody reads any more, like a pipe whose reader has gone.
	struct Failing final :
		public std::streambuf
	{
	protected:
		int overflow(int) override { return EOF; };
		std::streamsize xsputn(const char*, std::streamsize) override { return 0; };
		int sync() override { return -1; };
	};
};

TEST_SUITE("EditorLog")
{
	TEST_CASE("the log keeps every line when the stream it passes on to fails")
	{
		Failing failing;
		std::streambuf* const previous = std::cout.rdbuf(&failing);

		Loom::EditorLog::Get().Install();
		Loom::EditorLog::Get().Clear();

		std::cout << "first" << std::endl;
		std::cout << 's' << "econd" << std::endl;
		std::cout << "third" << std::endl;

		const bool still_good = std::cout.good();

		std::vector<std::string> lines;
		Loom::EditorLog::Get().ForEach([&](size_t, const Loom::EditorLog::Line& line) { lines.push_back(line.text); });

		Loom::EditorLog::Get().Clear();
		Loom::EditorLog::Get().Uninstall();
		std::cout.rdbuf(previous);

		CHECK(still_good);
		CHECK(lines == std::vector<std::string>{ "first", "second", "third" });
	};

	TEST_CASE("a line naming a file in the project names it from the project's folder, and others are left as written")
	{
		Loom::EditorLog::Get().Install();
		Loom::EditorLog::Get().Clear();
		Loom::EditorLog::Get().SetProject("C:\\Games\\Crash Demo\\");

		std::cerr << "Sprites: C:\\Games\\Crash Demo\\Assets\\Overfull.loomsprite is not a whole sprite" << std::endl;
		std::cerr << "Import: c:/games/crash demo/Assets/Some Models\\Broken.ply: Validation failed" << std::endl;
		std::cerr << "C:\\Games\\Crash Demo\\a.png and C:\\Games\\Crash Demo\\b.png" << std::endl;
		std::cerr << "C:\\Games\\Crash Demo2\\a.png, C:\\Games\\Crash Demo, C:\\Other\\a.png" << std::endl;

		std::vector<std::string> lines;
		Loom::EditorLog::Get().ForEach([&](size_t, const Loom::EditorLog::Line& line) { lines.push_back(line.text); });

		Loom::EditorLog::Get().SetProject("");
		Loom::EditorLog::Get().Clear();
		Loom::EditorLog::Get().Uninstall();

		CHECK(lines == std::vector<std::string>{
			"Sprites: Assets/Overfull.loomsprite is not a whole sprite",
			"Import: Assets/Some Models/Broken.ply: Validation failed",
			"a.png and b.png",
			"C:\\Games\\Crash Demo2\\a.png, C:\\Games\\Crash Demo, C:\\Other\\a.png" });
	};

	TEST_CASE("a line naming a file in the project links to it, with the line and column after it")
	{
		const Project project;

		std::cout << "  " << project.Path("Scripts/Laser.hpp") << "(111,1): error C2059: syntax error: 'this'" << std::endl;
		std::cerr << "Sprites: " << project.Path("Assets/Overfull.loomsprite") << " is not a whole sprite" << std::endl;
		std::cerr << "Import: " << project.folder.generic_string() << "/Assets/Some Models\\Broken.ply: Validation failed" << std::endl;
		std::cerr << project.Path("Scripts/Laser.hpp") << "(12): warning C4100" << std::endl;

		const std::vector<Loom::EditorLog::Line> lines = Project::Lines();

		REQUIRE(lines.size() == 4);

		for (const Loom::EditorLog::Line& line : lines)
			REQUIRE(line.links.size() == 1);

		CHECK(lines[0].text == "  Scripts/Laser.hpp(111,1): error C2059: syntax error: 'this'");
		CHECK(Project::Linked(lines[0], lines[0].links[0]) == "Scripts/Laser.hpp(111,1)");
		CHECK(lines[0].links[0].path == project.Path("Scripts/Laser.hpp"));
		CHECK(lines[0].links[0].line == 111);
		CHECK(lines[0].links[0].column == 1);

		CHECK(Project::Linked(lines[1], lines[1].links[0]) == "Assets/Overfull.loomsprite");
		CHECK(lines[1].links[0].path == project.Path("Assets/Overfull.loomsprite"));
		CHECK(lines[1].links[0].line == 0);

		CHECK(Project::Linked(lines[2], lines[2].links[0]) == "Assets/Some Models/Broken.ply");
		CHECK(lines[2].links[0].path == project.Path("Assets/Some Models/Broken.ply"));

		CHECK(Project::Linked(lines[3], lines[3].links[0]) == "Scripts/Laser.hpp(12)");
		CHECK(lines[3].links[0].line == 12);
		CHECK(lines[3].links[0].column == 0);
	};

	TEST_CASE("only a file that is in the project gets a link, and a line may name more than one")
	{
		const Project project;
		const std::string sibling = project.folder.string() + " Two\\Scripts\\Laser.hpp";

		std::cerr << project.Path("Scripts/Gone.hpp") << "(3,1): error C1083" << std::endl;
		std::cerr << "Watching " << project.Path("Scripts") << " for changes" << std::endl;
		std::cerr << "Opened " << project.folder.string() << std::endl;
		std::cerr << sibling << "(3,1): error C1083" << std::endl;
		std::cerr << project.Path("Scripts/Laser.hpp") << " and " << project.Path("Assets/Overfull.loomsprite") << std::endl;

		const std::vector<Loom::EditorLog::Line> lines = Project::Lines();

		REQUIRE(lines.size() == 5);
		CHECK(lines[0].text == "Scripts/Gone.hpp(3,1): error C1083");
		CHECK(lines[0].links.empty());
		CHECK(lines[1].text == "Watching Scripts for changes");
		CHECK(lines[1].links.empty());
		CHECK(lines[2].links.empty());
		CHECK(lines[3].text == sibling + "(3,1): error C1083");
		CHECK(lines[3].links.empty());

		REQUIRE(lines[4].links.size() == 2);
		CHECK(Project::Linked(lines[4], lines[4].links[0]) == "Scripts/Laser.hpp");
		CHECK(Project::Linked(lines[4], lines[4].links[1]) == "Assets/Overfull.loomsprite");
	};

	TEST_CASE("a location that is not a whole line, or line and column, is left out of the link")
	{
		const Project project;
		const std::vector<std::string> after{ "(12,)", "(x)", "(0)", "(5", "(5,2" };

		for (const std::string& location : after)
			std::cerr << project.Path("Scripts/Laser.hpp") << location << std::endl;

		const std::vector<Loom::EditorLog::Line> lines = Project::Lines();

		REQUIRE(lines.size() == after.size());

		for (const Loom::EditorLog::Line& line : lines)
		{
			REQUIRE(line.links.size() == 1);
			CHECK(Project::Linked(line, line.links[0]) == "Scripts/Laser.hpp");
			CHECK(line.links[0].line == 0);
		};
	};

	TEST_CASE("without a project no line has a link")
	{
		const Project project;
		Loom::EditorLog::Get().SetProject("");

		std::cerr << project.Path("Scripts/Laser.hpp") << "(1,1): error C2059" << std::endl;

		const std::vector<Loom::EditorLog::Line> lines = Project::Lines();

		REQUIRE(lines.size() == 1);
		CHECK(lines[0].links.empty());
	};
};
