#include "doctest.h"

#ifndef LOOM_FINAL

#include "Profiler.h"
#include "StageTiming.h"

#include "imgui.h"


namespace
{
	// Whether the profiler records once the overlay has drawn a frame, open
	// or collapsed.
	bool RecordsWith(bool collapsed)
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

		Loom::Profiler::SetRecording(!collapsed);

		ImGui::NewFrame();
		ImGui::SetNextWindowCollapsed(collapsed, ImGuiCond_Always);
		Loom::StageTiming::Overlay();
		ImGui::EndFrame();

		const bool recording = Loom::Profiler::Recording();

		Loom::Profiler::SetRecording(false);
		ImGui::DestroyContext(context);

		return recording;
	};
};

TEST_SUITE("StageTiming")
{
	TEST_CASE("the overlay records the stages while it is open")
	{
		CHECK(RecordsWith(false));
	};

	TEST_CASE("the overlay stops the recording while it is collapsed")
	{
		CHECK_FALSE(RecordsWith(true));
	};
};

#endif
