#include "doctest.h"

#include "Utilities/AxisGui.h"

#include "imgui.h"

#include <functional>


namespace
{
	constexpr int axis_count = 3;

	// ImGui::AddRectFilled with no rounding: one quad.
	constexpr int vertices_per_stripe = 4;

	constexpr float drag_speed = 0.01f;

	// A window spends its first frame hidden while it sizes itself, so what
	// it draws is only measured on the frames after.
	constexpr int settle_frames = 2;

	struct ImGuiFrames
	{
		ImGuiFrames()
		{
			constexpr float width = 800.0f;
			constexpr float height = 600.0f;
			constexpr float frame_time = 1.0f / 60.0f;

			context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(width, height);
			io.DeltaTime = frame_time;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);
		};

		~ImGuiFrames()
		{
			ImGui::DestroyContext(context);
		};

		// Vertices draw adds to a fixed-size window once it has settled.
		static int VerticesAdded(const std::function<void()>& draw, bool collapsed = false)
		{
			constexpr float window_width = 400.0f;
			constexpr float window_height = 300.0f;

			int added = 0;

			for (int frame = 0; frame < settle_frames; frame++)
			{
				ImGui::NewFrame();

				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(window_width, window_height));
				ImGui::SetNextWindowCollapsed(collapsed);
				ImGui::Begin("Inspector");

				const int before = ImGui::GetWindowDrawList()->VtxBuffer.Size;

				draw();

				added = ImGui::GetWindowDrawList()->VtxBuffer.Size - before;

				ImGui::End();
				ImGui::EndFrame();
			};

			return added;
		};

		ImGuiContext* context;
	};
};

TEST_SUITE("AxisGui")
{
	TEST_CASE("draws ImGui's own drag boxes plus one stripe per axis")
	{
		ImGuiFrames frames;

		for (int count = 2; count <= 4; count++)
		{
			CAPTURE(count);

			float plain[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
			float striped[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
			bool changed = true;

			const int plain_vertices = ImGuiFrames::VerticesAdded([&]()
			{
				ImGui::DragScalarN("##value", ImGuiDataType_Float, plain, count, drag_speed);
			});

			const int striped_vertices = ImGuiFrames::VerticesAdded([&]()
			{
				changed = Loom::AxisGui::DragFloatN("##value", striped, count, drag_speed);
			});

			CHECK_FALSE(changed);
			CHECK(striped_vertices - plain_vertices == vertices_per_stripe * (count < axis_count ? count : axis_count));
		};
	};

	TEST_CASE("a visible label is drawn after the boxes, as ImGui does")
	{
		ImGuiFrames frames;

		float plain[axis_count] = { };
		float striped[axis_count] = { };

		const int plain_vertices = ImGuiFrames::VerticesAdded([&]()
		{
			ImGui::DragScalarN("Position", ImGuiDataType_Float, plain, axis_count, drag_speed);
		});

		const int striped_vertices = ImGuiFrames::VerticesAdded([&]()
		{
			Loom::AxisGui::DragFloatN("Position", striped, axis_count, drag_speed);
		});

		CHECK(striped_vertices - plain_vertices == vertices_per_stripe * axis_count);
	};

	TEST_CASE("a collapsed window draws nothing")
	{
		ImGuiFrames frames;

		float values[axis_count] = { };
		bool changed = true;

		const int vertices = ImGuiFrames::VerticesAdded([&]()
		{
			changed = Loom::AxisGui::DragFloatN("Position", values, axis_count, drag_speed);
		}, true);

		CHECK_FALSE(changed);
		CHECK(vertices == 0);
	};
};
