#include "doctest.h"

#ifndef LOOM_FINAL

#include "Profiler.h"
#include "StageTiming.h"

#include "imgui.h"

#include <array>


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
};

#endif
