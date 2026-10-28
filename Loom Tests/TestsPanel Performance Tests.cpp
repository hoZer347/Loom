#include "doctest.h"

#include "TestsPanel.h"

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <sstream>
#include <string>


#pragma region Helpers

namespace
{
	constexpr size_t CASES = 10000;
	constexpr size_t FILES = 200;

	// One case in this many fails, so the tree holds failure rows too.
	constexpr size_t FAILING_EVERY = 10;

	constexpr float WIDTH = 1600.0f;
	constexpr float HEIGHT = 400.0f;
	constexpr float FRAME_TIME = 1.0f / 60.0f;

	// A step is timed this many times and the quickest kept, so a thread
	// switched out mid-step costs a retry rather than the result.
	constexpr int TRIES = 5;

	// A new window or table skips drawing its first frames while it sizes
	// itself, so these come first, untimed.
	constexpr int WARM_UP = 3;

	using Clock = std::chrono::steady_clock;

	double MillisecondsSince(Clock::time_point start)
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	};

	// CASES cases over FILES files, taken back when it ends.
	struct Suite
	{
		const size_t before = Loom::ScriptTests::All().size();
		double registering = 0.0;

		Suite()
		{
			const Clock::time_point start = Clock::now();

			for (size_t i = 0; i < CASES; ++i)
			{
				const bool fails = i % FAILING_EVERY == 0;
				const Loom::ScriptTests::Body body = fails ? Loom::ScriptTests::Body([] { LOOM_CHECK(false); }) : Loom::ScriptTests::Body([] { });

				Loom::ScriptTests::Register("Performance case " + std::to_string(i), Path(i % FILES), (int)(i / FILES) + 1, body);
			};

			registering = MillisecondsSince(start);
		};

		~Suite()
		{
			Loom::ScriptTests::Truncate(before);
		};

		static const char* Path(size_t file)
		{
			static const std::vector<std::string> paths = []
			{
				std::vector<std::string> paths;

				for (size_t i = 0; i < FILES; ++i)
					paths.push_back("Scripts/Performance/File" + std::to_string(i) + ".hpp");

				return paths;
			}();

			return paths[file].c_str();
		};
	};

	// The panel drawn into its own ImGui context, the size of a docked panel.
	struct Frames
	{
		ImGuiContext* const context = ImGui::CreateContext();
		bool open = true;
		const Loom::TestsPanel::Callbacks callbacks{ [] { }, [](const std::string&, int) { } };

		Frames()
		{
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(WIDTH, HEIGHT);
			io.DeltaTime = FRAME_TIME;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);
		};

		~Frames()
		{
			ImGui::DestroyContext(context);
		};

		void Frame(Loom::TestsPanel& panel)
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
			panel.Draw(&open, callbacks);
			ImGui::EndFrame();
		};

		// The quickest of TRIES frames.
		double Quickest(Loom::TestsPanel& panel)
		{
			double quickest = std::numeric_limits<double>::max();

			for (int i = 0; i < WARM_UP; ++i)
				Frame(panel);

			for (int i = 0; i < TRIES; ++i)
			{
				const Clock::time_point start = Clock::now();

				Frame(panel);
				quickest = std::min(quickest, MillisecondsSince(start));
			};

			return quickest;
		};
	};
};

#pragma endregion


TEST_SUITE("Performance" * doctest::skip())
{
	#pragma region Tests panel

	TEST_CASE("ten thousand test cases register, draw and run without holding up a frame")
	{
		constexpr double most_registering = 20.0;
		constexpr double most_frame = 1.0;

		// The longest any one RunQueued may take, as the editor calls it once a frame.
		constexpr double most_run_step = 20.0;

		// The one frame that lays the tree out again once results come in or the filter changes.
		constexpr double most_rebuilding = 10.0;

		const Suite suite;
		Frames frames;
		Loom::TestsPanel panel;

		const double not_run = frames.Quickest(panel);

		panel.QueueShown();

		double longest_step = 0.0;
		int steps = 0;
		const Clock::time_point run_start = Clock::now();

		while (panel.HasQueued())
		{
			const Clock::time_point start = Clock::now();

			panel.RunQueued();
			longest_step = std::max(longest_step, MillisecondsSince(start));
			++steps;
		};

		const double running = MillisecondsSince(run_start);
		const Clock::time_point rebuild_start = Clock::now();

		frames.Frame(panel);

		const double rebuilding = MillisecondsSince(rebuild_start);
		const double run = frames.Quickest(panel);

		panel.SetFilter("case 99");

		const Clock::time_point refilter_start = Clock::now();

		frames.Frame(panel);

		const double refiltering = MillisecondsSince(refilter_start);
		const double filtered = frames.Quickest(panel);

		std::ostringstream report;
		report << CASES << " cases in " << FILES << " files: registered in " << suite.registering << " ms, a frame takes "
			<< not_run << " ms before they run, " << run << " ms after and " << filtered << " ms filtered; they ran in "
			<< running << " ms over " << steps << " steps, the longest " << longest_step << " ms, and the frame that took in their results "
			<< rebuilding << " ms, and the one that took in the filter " << refiltering << " ms";
		MESSAGE(report.str());

		REQUIRE(panel.ResultOf("Performance case 0"));
		CHECK(panel.ResultOf("Performance case 0")->outcome == Loom::ScriptTests::Outcome::Failed);
		REQUIRE(panel.ResultOf("Performance case " + std::to_string(CASES - 1)));
		CHECK(suite.registering <= most_registering);
		CHECK(not_run <= most_frame);
		CHECK(run <= most_frame);
		CHECK(filtered <= most_frame);
		CHECK(longest_step <= most_run_step);
		CHECK(rebuilding <= most_rebuilding);
		CHECK(refiltering <= most_rebuilding);
	};

	#pragma endregion
};
