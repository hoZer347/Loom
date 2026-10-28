#include "doctest.h"

#include "TestsPanel.h"

#include "EditorIcons.h"

#include "imgui.h"
#include "imgui_internal.h"

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

		Suite()
		{
			constexpr int line = 3;

			Loom::ScriptTests::Register("TestsPanel laser stops at a wall", "Scripts/Laser.hpp", line, [this] { ++laser_runs; });
			Loom::ScriptTests::Register("TestsPanel laser misses", "Scripts/Laser.hpp", line, [this] { ++laser_runs; LOOM_CHECK(laser_runs < 0); });
			Loom::ScriptTests::Register("TestsPanel door opens", "Scripts/Door.hpp", line, [this] { ++door_runs; });
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

	TEST_CASE("the panel draws its cases and their failures")
	{
		constexpr float width = 800.0f;
		constexpr float height = 400.0f;
		constexpr float frame_time = 1.0f / 60.0f;

		Suite suite;
		Loom::TestsPanel panel;
		bool open = true;
		int created = 0;

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

		const Loom::TestsPanel::Callbacks callbacks{ [&created] { ++created; }, [](const std::string&, int) { } };

		for (int frame = 0; frame < 2; ++frame)
		{
			ImGui::NewFrame();
			panel.Draw(&open, callbacks);
			ImGui::EndFrame();
		};

		CHECK(ImGui::FindWindowByName(Loom::EditorPanels::Tests) != nullptr);
		CHECK(open);
		CHECK(created == 0);

		ImGui::DestroyContext(context);
	};
};
