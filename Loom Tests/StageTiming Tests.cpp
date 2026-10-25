#include "doctest.h"

#ifndef LOOM_FINAL

#include "Profiler.h"
#include "StageTiming.h"

#include "imgui.h"

#include <array>
#include <vector>


namespace
{
	// A window sizes itself on its first frame, so the second is the one it
	// keeps to.
	constexpr int frames = 2;

	// Whether the profiler records after each of the overlay's first frames,
	// open or collapsed, having been left the other way.
	std::array<bool, frames> RecordsWith(bool collapsed)
	{
		constexpr float width = 800.0f;
		constexpr float height = 600.0f;
		constexpr float frame_time = 1.0f / 60.0f;

		ImGuiContext* context = ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(width, height);
		io.DeltaTime = frame_time;

		unsigned char* pixels = nullptr;
		int atlas_width = 0;
		int atlas_height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);

		std::array<bool, frames> recording{ };

		for (bool& recorded : recording)
		{
			Loom::Profiler::SetRecording(collapsed);

			ImGui::NewFrame();
			ImGui::SetNextWindowCollapsed(collapsed, ImGuiCond_Always);
			Loom::StageTiming::Overlay();
			ImGui::EndFrame();

			recorded = Loom::Profiler::Recording();
		};

		Loom::Profiler::SetRecording(false);
		ImGui::DestroyContext(context);

		return recording;
	};

	Loom::Profiler::Row Row(int depth, double average)
	{
		return { "", depth, average, average, { }, { } };
	};

	void CheckShare(const Loom::StageTiming::Share& share, double from, double to)
	{
		CHECK(share.from == doctest::Approx(from));
		CHECK(share.to == doctest::Approx(to));
	};
};

TEST_SUITE("StageTiming")
{
	TEST_CASE("the overlay records the stages while it is open")
	{
		for (const bool recorded : RecordsWith(false))
			CHECK(recorded);
	};

	TEST_CASE("the overlay stops the recording while it is collapsed")
	{
		for (const bool recorded : RecordsWith(true))
			CHECK_FALSE(recorded);
	};

	TEST_CASE("the stages share the whole width by their averages")
	{
		const std::vector<Loom::StageTiming::Share> shares = Loom::StageTiming::Shares({ Row(0, 1.0), Row(0, 3.0) });

		REQUIRE(shares.size() == 2);
		CheckShare(shares[0], 0.0, 0.25);
		CheckShare(shares[1], 0.25, 1.0);
	};

	TEST_CASE("a stage's parts fill it from the left at the same scale")
	{
		const std::vector<Loom::StageTiming::Share> shares = Loom::StageTiming::Shares({
			Row(0, 4.0),
			Row(1, 1.0),
			Row(2, 0.5),
			Row(1, 2.0),
			Row(0, 4.0) });

		REQUIRE(shares.size() == 5);
		CheckShare(shares[0], 0.0, 0.5);
		CheckShare(shares[1], 0.0, 0.125);
		CheckShare(shares[2], 0.0, 0.0625);
		CheckShare(shares[3], 0.125, 0.375);
		CheckShare(shares[4], 0.5, 1.0);
	};

	TEST_CASE("a part never runs past its stage")
	{
		const std::vector<Loom::StageTiming::Share> shares = Loom::StageTiming::Shares({ Row(0, 1.0), Row(1, 2.0), Row(0, 1.0) });

		REQUIRE(shares.size() == 3);
		CheckShare(shares[1], 0.0, 0.5);
		CheckShare(shares[2], 0.5, 1.0);
	};

	TEST_CASE("rows that took no time share none of the width")
	{
		const std::vector<Loom::StageTiming::Share> shares = Loom::StageTiming::Shares({ Row(0, 0.0), Row(1, 0.0) });

		REQUIRE(shares.size() == 2);

		for (const Loom::StageTiming::Share& share : shares)
			CheckShare(share, 0.0, 0.0);
	};
};

#endif
