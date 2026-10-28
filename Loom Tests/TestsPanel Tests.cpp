#include "doctest.h"

#include "TestsPanel.h"

#include "EditorIcons.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <string>
#include <vector>


#pragma region Helpers

namespace
{
	// Three cases of the test's own, taken back when it ends.
	struct Suite
	{
		const size_t before = Loom::ScriptTests::All().size();
		int laser_runs = 0;
		int door_runs = 0;
		int failure_line = 0;

		Suite()
		{
			constexpr int stops_line = 3;
			constexpr int misses_line = 4;
			constexpr int door_line = 5;

			Loom::ScriptTests::Register("TestsPanel laser stops at a wall", "Scripts/Laser.hpp", stops_line, [this] { ++laser_runs; });
			Loom::ScriptTests::Register("TestsPanel laser misses", "Scripts/Laser.hpp", misses_line, [this] { ++laser_runs; failure_line = __LINE__; LOOM_CHECK(laser_runs < 0); });
			Loom::ScriptTests::Register("TestsPanel door opens", "Scripts/Door.hpp", door_line, [this] { ++door_runs; });
		};

		~Suite()
		{
			Loom::ScriptTests::Truncate(before);
		};
	};

	std::vector<std::string> Names(const std::vector<const Loom::ScriptTests::Case*>& cases)
	{
		std::vector<std::string> names;

		for (const Loom::ScriptTests::Case* test : cases)
			names.push_back(test->name);

		return names;
	};
};

#pragma endregion


TEST_SUITE("TestsPanel")
{
	TEST_CASE("the filter narrows the cases shown")
	{
		const Suite suite;
		Loom::TestsPanel panel;

		panel.SetFilter("TestsPanel LASER");
		CHECK(Names(panel.Shown()) == std::vector<std::string>{ "TestsPanel laser stops at a wall", "TestsPanel laser misses" });

		panel.SetFilter("TestsPanel");
		CHECK(panel.Shown().size() == 3);

		panel.SetFilter("TestsPanel nothing");
		CHECK(panel.Shown().empty());
	};

	TEST_CASE("Run takes the cases shown, and only once RunQueued runs them")
	{
		Suite suite;
		Loom::TestsPanel panel;

		panel.SetFilter("TestsPanel laser");
		panel.QueueShown();

		CHECK(suite.laser_runs == 0);
		CHECK(panel.ResultOf("TestsPanel laser misses") == nullptr);

		panel.RunQueued();

		CHECK(suite.laser_runs == 2);
		CHECK(suite.door_runs == 0);
		REQUIRE(panel.ResultOf("TestsPanel laser stops at a wall"));
		CHECK(panel.ResultOf("TestsPanel laser stops at a wall")->outcome == Loom::ScriptTests::Outcome::Passed);
		REQUIRE(panel.ResultOf("TestsPanel laser misses"));
		CHECK(panel.ResultOf("TestsPanel laser misses")->outcome == Loom::ScriptTests::Outcome::Failed);
		CHECK(panel.ResultOf("TestsPanel door opens") == nullptr);

		panel.RunQueued();
		CHECK(suite.laser_runs == 2);
	};

	TEST_CASE("a queued case that unloaded before it ran is passed over")
	{
		Loom::TestsPanel panel;

		{
			const Suite suite;

			panel.SetFilter("TestsPanel door");
			panel.QueueShown();
		};

		panel.RunQueued();
		CHECK(panel.ResultOf("TestsPanel door opens") == nullptr);
	};

	TEST_CASE("a case the filter shows that loaded after Run is run too")
	{
		Loom::TestsPanel panel;

		panel.SetFilter("TestsPanel door");
		panel.QueueShown();

		Suite suite;

		CHECK(panel.HasQueued());
		panel.RunQueued();

		CHECK_FALSE(panel.HasQueued());
		CHECK(suite.door_runs == 1);
		CHECK(suite.laser_runs == 0);
	};

	TEST_CASE("clicking down the test column follows each case and the failure under it")
	{
		constexpr float width = 800.0f;
		constexpr float height = 400.0f;
		constexpr float frame_time = 1.0f / 60.0f;
		constexpr float click_step = 4.0f;

		Suite suite;
		Loom::TestsPanel panel;
		bool open = true;
		int created = 0;
		std::vector<std::string> followed;

		ImGuiContext* const context = ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(width, height);
		io.DeltaTime = frame_time;

		// Clicks this close together would otherwise double-click the title bar shut.
		io.MouseDoubleClickTime = 0.0f;

		unsigned char* pixels = nullptr;
		int atlas_width = 0;
		int atlas_height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);

		panel.SetFilter("TestsPanel");
		panel.QueueShown();
		panel.RunQueued();

		const Loom::TestsPanel::Callbacks callbacks{
			[&created] { ++created; },
			[&followed](const std::string& file, int line) { followed.push_back(file.substr(file.find_last_of("/\\") + 1) + ":" + std::to_string(line)); } };

		const auto frame =
			[&]
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(io.DisplaySize);
				panel.Draw(&open, callbacks);
				ImGui::EndFrame();
			};

		frame();

		// Down the middle of the panel, which is the test column below its toolbar.
		for (float y = 0.0f; y < height; y += click_step)
		{
			io.AddMousePosEvent(width / 2.0f, y);
			frame();
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			frame();
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			frame();
		};

		followed.erase(std::unique(followed.begin(), followed.end()), followed.end());

		std::string order;

		for (const std::string& place : followed)
			order += place + " ";

		CHECK(order == "Laser.hpp:3 Laser.hpp:4 TestsPanel Tests.cpp:" + std::to_string(suite.failure_line) + " Door.hpp:5 ");
		CHECK_FALSE(panel.HasQueued());
		CHECK(open);
		CHECK(created == 0);

		ImGui::DestroyContext(context);
	};
};
