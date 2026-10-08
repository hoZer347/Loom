#include "doctest.h"

#include "EditorLayouts.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
	constexpr const char* window = "Layout Test Window";

	// An ImGui context of the test's own, with no window and no renderer, and
	// an empty folder for layouts.
	struct Driven
	{
		Driven() :
			folder(std::filesystem::temp_directory_path() / "EditorLayouts Tests")
		{
			constexpr float frame_time = 1.0f / 60.0f;
			constexpr float display_size = 800.0f;

			std::filesystem::remove_all(folder);

			context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(display_size, display_size);
			io.DeltaTime = frame_time;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);
		};

		~Driven()
		{
			ImGui::DestroyContext(context);
			std::filesystem::remove_all(folder);
		};

		// Draws the window, placed at `position` when one is given.
		void Frame(const ImVec2* position = nullptr)
		{
			ImGui::NewFrame();

			if (position)
				ImGui::SetNextWindowPos(*position);

			ImGui::Begin(window);
			ImGui::End();
			ImGui::EndFrame();
		};

		static ImVec2 Position()
		{
			return ImGui::FindWindowByName(window)->Pos;
		};

		std::filesystem::path folder;
		ImGuiContext* context = nullptr;
	};
};

TEST_SUITE("EditorLayouts")
{
	TEST_CASE("names are file names: letters, digits, spaces, _ and -, no space at either end")
	{
		CHECK(Loom::EditorLayouts::IsValidName("Wide Scene"));
		CHECK(Loom::EditorLayouts::IsValidName("debug_2-up"));

		CHECK_FALSE(Loom::EditorLayouts::IsValidName(""));
		CHECK_FALSE(Loom::EditorLayouts::IsValidName(" Leading"));
		CHECK_FALSE(Loom::EditorLayouts::IsValidName("Trailing "));
		CHECK_FALSE(Loom::EditorLayouts::IsValidName("a/b"));
		CHECK_FALSE(Loom::EditorLayouts::IsValidName("..\\up"));
		CHECK_FALSE(Loom::EditorLayouts::IsValidName("what?"));
		CHECK_FALSE(Loom::EditorLayouts::IsValidName(std::string(Loom::EditorLayouts::MaxNameLength + 1, 'a')));
	};

	TEST_CASE("Windows' device names are not names, in any case")
	{
		CHECK_FALSE(Loom::EditorLayouts::IsValidName("NUL"));
		CHECK_FALSE(Loom::EditorLayouts::IsValidName("con"));
		CHECK_FALSE(Loom::EditorLayouts::IsValidName("Aux"));
		CHECK_FALSE(Loom::EditorLayouts::IsValidName("com1"));
		CHECK_FALSE(Loom::EditorLayouts::IsValidName("LPT9"));

		CHECK(Loom::EditorLayouts::IsValidName("Console"));
		CHECK(Loom::EditorLayouts::IsValidName("COM"));
		CHECK(Loom::EditorLayouts::IsValidName("COMX"));
	};

	TEST_CASE("names that differ only in case are the same file")
	{
		CHECK(Loom::EditorLayouts::SameName("Alpha", "aLPHA"));
		CHECK_FALSE(Loom::EditorLayouts::SameName("Alpha", "Alphas"));
	};

	TEST_CASE("loading names the windows the layout has a place for")
	{
		Driven driven;
		driven.Frame();

		REQUIRE(Loom::EditorLayouts::Save(driven.folder, "Named"));

		std::vector<ImGuiID> windows;
		REQUIRE(Loom::EditorLayouts::Load(driven.folder, "Named", &windows));

		CHECK(std::find(windows.begin(), windows.end(), ImHashStr(window)) != windows.end());
		CHECK(std::find(windows.begin(), windows.end(), ImHashStr("Never Drawn")) == windows.end());
	};

	TEST_CASE("a saved layout is listed by name, sorted, and other files are not")
	{
		Driven driven;
		driven.Frame();

		CHECK(Loom::EditorLayouts::Names(driven.folder).empty());

		REQUIRE(Loom::EditorLayouts::Save(driven.folder, "Zed"));
		REQUIRE(Loom::EditorLayouts::Save(driven.folder, "Alpha"));
		std::ofstream(driven.folder / "notes.txt") << "not a layout";

		CHECK(Loom::EditorLayouts::Names(driven.folder) == std::vector<std::string>{ "Alpha", "Zed" });
	};

	TEST_CASE("an unusable name is not saved")
	{
		Driven driven;
		driven.Frame();

		CHECK_FALSE(Loom::EditorLayouts::Save(driven.folder, "a/b"));
		CHECK(Loom::EditorLayouts::Names(driven.folder).empty());
	};

	TEST_CASE("loading puts a window back where it was when the layout was saved")
	{
		const ImVec2 saved_at(40.0f, 60.0f);
		const ImVec2 moved_to(300.0f, 200.0f);

		Driven driven;
		driven.Frame(&saved_at);
		driven.Frame();

		REQUIRE(Loom::EditorLayouts::Save(driven.folder, "Saved"));

		driven.Frame(&moved_to);
		REQUIRE(Driven::Position().x == moved_to.x);

		REQUIRE(Loom::EditorLayouts::Load(driven.folder, "Saved"));
		driven.Frame();

		CHECK(Driven::Position().x == saved_at.x);
		CHECK(Driven::Position().y == saved_at.y);
	};

	TEST_CASE("loading in the middle of a frame, before the dockspace, puts a window back in its dock")
	{
		constexpr float split = 0.5f;
		constexpr const char* other = "Layout Test Other";

		Driven driven;
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		enum class Dock { Keep, Apart, Together };

		// One frame of the editor's order: a layout loaded, the dockspace, then
		// the windows in it.
		const auto frame = [&](Dock dock, const char* load = nullptr)
		{
			ImGui::NewFrame();

			if (load)
				REQUIRE(Loom::EditorLayouts::Load(driven.folder, load));

			const ImGuiID dockspace = ImGui::DockSpaceOverViewport();

			if (dock != Dock::Keep)
			{
				ImGui::DockBuilderRemoveNodeChildNodes(dockspace);

				ImGuiID left = dockspace;
				const ImGuiID right = dock == Dock::Apart
					? ImGui::DockBuilderSplitNode(left, ImGuiDir_Right, split, nullptr, &left)
					: left;

				ImGui::DockBuilderDockWindow(window, left);
				ImGui::DockBuilderDockWindow(other, right);
				ImGui::DockBuilderFinish(dockspace);
			};

			ImGui::Begin(window);
			ImGui::End();
			ImGui::Begin(other);
			ImGui::End();
			ImGui::EndFrame();
		};

		const auto apart = []()
		{
			return ImGui::FindWindowByName(window)->DockId != ImGui::FindWindowByName(other)->DockId;
		};

		frame(Dock::Apart);
		frame(Dock::Keep);
		REQUIRE(apart());

		REQUIRE(Loom::EditorLayouts::Save(driven.folder, "Apart"));

		frame(Dock::Together);
		frame(Dock::Keep);
		REQUIRE_FALSE(apart());

		frame(Dock::Keep, "Apart");
		frame(Dock::Keep);

		CHECK(apart());
		CHECK(static_cast<bool>(ImGui::FindWindowByName(window)->DockIsActive));
		CHECK(static_cast<bool>(ImGui::FindWindowByName(other)->DockIsActive));
	};

	TEST_CASE("a missing layout does not load, and a deleted one is no longer listed")
	{
		Driven driven;
		driven.Frame();

		CHECK_FALSE(Loom::EditorLayouts::Load(driven.folder, "Missing"));

		REQUIRE(Loom::EditorLayouts::Save(driven.folder, "Gone"));
		CHECK(Loom::EditorLayouts::Delete(driven.folder, "Gone"));
		CHECK(Loom::EditorLayouts::Names(driven.folder).empty());
		CHECK_FALSE(Loom::EditorLayouts::Delete(driven.folder, "Gone"));
	};
};
