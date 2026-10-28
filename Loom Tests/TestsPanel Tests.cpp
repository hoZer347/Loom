#include "doctest.h"

#include "TestsPanel.h"

#include "imgui.h"

#include <chrono>
#include <string>
#include <thread>
#include <utility>
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
		REQUIRE(panel.LastRun());
		CHECK(panel.LastRun()->cases == 0);
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

	TEST_CASE("rows select as files do, and a double click follows a case or a failure")
	{
		constexpr float width = 800.0f;
		constexpr float height = 400.0f;
		constexpr float frame_time = 1.0f / 60.0f;
		constexpr float scan_step = 2.0f;
		constexpr float double_click_time = 0.3f;
		constexpr double most_clicked_run_milliseconds = 1000.0;

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

		const auto click =
			[&](float y, ImGuiKey modifier = ImGuiKey_None, float x = -1.0f)
			{
				if (modifier != ImGuiKey_None)
					io.AddKeyEvent(modifier, true);

				io.AddMousePosEvent(x < 0.0f ? width / 2.0f : x, y);
				frame();
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
				frame();
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
				frame();

				if (modifier != ImGuiKey_None)
					io.AddKeyEvent(modifier, false);

				frame();
			};

		const auto chosen =
			[&]
			{
				std::string names;

				for (const std::string& name : Names(panel.Chosen()))
					names += name + "|";

				return names;
			};

		const std::string everything = chosen();

		// Clicks this close together would otherwise double-click.
		io.MouseDoubleClickTime = 0.0f;
		frame();

		// Run, found by clicking across the toolbar below the title bar, past New.
		ImVec2 run{ };

		for (float y = ImGui::GetFrameHeight() + scan_step; y < height && !panel.HasQueued(); y += scan_step)
			for (float x = scan_step; x < width && !panel.HasQueued(); x += scan_step)
			{
				click(y, ImGuiKey_None, x);
				run = ImVec2(x, y);
			};

		REQUIRE(panel.HasQueued());
		CHECK(created > 0);
		created = 0;
		panel.RunQueued();

		// A clicked run names its cases, and is timed from its first case all the same.
		CHECK_FALSE(panel.HasQueued());
		REQUIRE(panel.LastRun());
		CHECK(panel.LastRun()->milliseconds < most_clicked_run_milliseconds);

		// Each row's y, found by clicking down the panel and reading what a click chose.
		std::vector<std::pair<float, std::string>> rows;

		for (float y = 0.0f; y < height; y += scan_step)
		{
			click(y);

			const std::string now = chosen();

			if (now != everything && (rows.empty() || rows.back().second != now))
				rows.emplace_back(y, now);
		};

		// Door.hpp's row and its one case's row choose the same, so they read as one.
		REQUIRE(rows.size() == 4);
		CHECK(rows[0].second == "TestsPanel laser stops at a wall|TestsPanel laser misses|");
		CHECK(rows[1].second == "TestsPanel laser stops at a wall|");
		CHECK(rows[2].second == "TestsPanel laser misses|");
		CHECK(rows[3].second == "TestsPanel door opens|");
		CHECK(followed.empty());

		click(rows[1].first);
		click(rows[3].first, ImGuiMod_Ctrl);
		CHECK(chosen() == "TestsPanel laser stops at a wall|TestsPanel door opens|");

		click(rows[2].first);
		click(rows[3].first, ImGuiMod_Shift);
		CHECK(chosen() == "TestsPanel laser misses|TestsPanel door opens|");

		click(rows[1].first, ImGuiMod_Ctrl);
		click(rows[1].first, ImGuiMod_Ctrl);
		CHECK(chosen() == "TestsPanel laser misses|TestsPanel door opens|");

		io.MouseDoubleClickTime = double_click_time;

		const auto double_click =
			[&](float y)
			{
				click(y);
				click(y);

				// Long enough that the next click starts a new double click.
				io.DeltaTime = double_click_time;
				frame();
				io.DeltaTime = frame_time;
			};

		// Up from Door.hpp's row, the first row a double click follows is the failure's.
		for (float y = rows[3].first - scan_step; y > rows[2].first && followed.empty(); y -= scan_step)
			double_click(y);

		double_click(rows[1].first);

		std::string order;

		for (const std::string& place : followed)
			order += place + " ";

		CHECK(order == "TestsPanel Tests.cpp:" + std::to_string(suite.failure_line) + " Laser.hpp:3 ");

		io.MouseDoubleClickTime = 0.0f;
		click(rows[3].first);
		click(rows[1].first, ImGuiMod_Ctrl);

		const int laser_runs = suite.laser_runs;
		const int door_runs = suite.door_runs;

		click(run.y, ImGuiKey_None, run.x);
		CHECK(panel.HasQueued());
		panel.RunQueued();

		CHECK(suite.laser_runs == laser_runs + 1);
		CHECK(suite.door_runs == door_runs + 1);
		CHECK(open);
		CHECK(created == 0);

		// Laser.hpp's arrow, found by clicking along its row until the row under it is Door.hpp's.
		float arrow = 0.0f;

		for (float x = scan_step; x < width && arrow == 0.0f; x += scan_step)
		{
			click(rows[0].first, ImGuiKey_None, x);
			click(rows[1].first);

			if (chosen() == "TestsPanel door opens|")
				arrow = x;
		};

		REQUIRE(arrow > 0.0f);
		click(rows[0].first);
		CHECK(chosen() == "TestsPanel laser stops at a wall|TestsPanel laser misses|");

		click(rows[0].first, ImGuiKey_None, arrow);
		click(rows[1].first);
		CHECK(chosen() == "TestsPanel laser stops at a wall|");

		ImGui::DestroyContext(context);
	};

	TEST_CASE("a long run takes several calls of RunQueued, each a part of it")
	{
		constexpr int cases = 20;
		constexpr std::chrono::milliseconds case_time{ 2 };
		constexpr std::chrono::milliseconds between_calls{ 5 };

		const size_t before = Loom::ScriptTests::All().size();
		Loom::TestsPanel panel;
		int runs = 0;
		int calls = 0;

		for (int i = 0; i < cases; ++i)
			Loom::ScriptTests::Register("TestsPanel slow case " + std::to_string(i), "Scripts/Slow.hpp", i + 1,
				[&runs, case_time]
				{
					const auto end = std::chrono::steady_clock::now() + case_time;

					while (std::chrono::steady_clock::now() < end);

					++runs;
				});

		panel.SetFilter("TestsPanel slow");
		panel.QueueShown();

		REQUIRE(panel.RunProgress());
		CHECK(panel.RunProgress()->total == 0);
		CHECK_FALSE(panel.LastRun());

		panel.RunQueued();
		++calls;
		REQUIRE(panel.HasQueued());
		REQUIRE(panel.RunProgress());
		CHECK(panel.RunProgress()->done == (size_t)runs);
		CHECK(panel.RunProgress()->total == cases);

		// A run asked for while one goes on is turned away, rather than cutting the first short.
		panel.SetFilter("TestsPanel slow case 0");
		panel.QueueShown();

		while (panel.HasQueued())
		{
			std::this_thread::sleep_for(between_calls);
			panel.RunQueued();
			++calls;
			CHECK((runs < cases) == panel.HasQueued());
		};

		CHECK(calls > 1);
		CHECK(runs == cases);
		CHECK(panel.ResultOf("TestsPanel slow case " + std::to_string(cases - 1)));
		CHECK_FALSE(panel.RunProgress());
		REQUIRE(panel.LastRun());
		CHECK(panel.LastRun()->cases == cases);
		CHECK(panel.LastRun()->milliseconds >= std::chrono::duration<double, std::milli>(case_time * cases + between_calls * (calls - 1)).count());
		CHECK(panel.LastRun()->at.size() == std::string("00:00:00").size());

		// A second run clears its cases' results, and fills them in as each finishes.
		panel.SetFilter("TestsPanel slow");
		panel.QueueShown();
		panel.RunQueued();

		REQUIRE(panel.RunProgress());
		const size_t done = panel.RunProgress()->done;
		size_t results = 0;

		for (int i = 0; i < cases; ++i)
			results += panel.ResultOf("TestsPanel slow case " + std::to_string(i)) != nullptr;

		CHECK(done < cases);
		CHECK(results == done);
		CHECK((size_t)runs == cases + done);

		while (panel.HasQueued())
			panel.RunQueued();

		Loom::ScriptTests::Truncate(before);
	};
};
