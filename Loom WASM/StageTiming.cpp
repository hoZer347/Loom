#include "StageTiming.h"

#ifndef LOOM_FINAL

#include "Profiler.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>


namespace Loom
{
	namespace
	{
		// The thinnest a stage's bar or graph is drawn.
		constexpr float stage_narrowest_bar = 1.0f;

		// Each stage keeps its colour on every thread, a soft one under dark
		// text. Hues step round the wheel by the golden ratio as stages are
		// first seen, so those seen together stay far apart.
		ImU32 StageColour(const std::string& name)
		{
			constexpr float hue_step = 0.618034f;
			constexpr float saturation = 0.45f;
			constexpr float value = 0.9f;

			static std::unordered_map<std::string, int> seen{ };

			const int order = seen.try_emplace(name, (int)seen.size()).first->second;
			float r = 0.0f;
			float g = 0.0f;
			float b = 0.0f;

			ImGui::ColorConvertHSVtoRGB(std::fmod(hue_step * (float)order, 1.0f), saturation, value, r, g, b);

			return ImGui::GetColorU32(ImVec4(r, g, b, 1.0f));
		};

		// What heads the lanes: the frame they average, in milliseconds.
		std::string AverageFrame(double frame)
		{
			constexpr size_t longest = 64;

			char text[longest];
			std::snprintf(text, longest, "An average frame of the last %d, %.2f ms", Profiler::Window, frame);

			return text;
		};
	};

	std::vector<StageTiming::Share> StageTiming::Shares(const std::vector<Profiler::Row>& rows)
	{
		double total = 0.0;
		int deepest = 0;

		for (const Profiler::Row& row : rows)
		{
			if (row.depth == 0)
				total += row.average;

			deepest = (std::max)(deepest, row.depth);
		};

		std::vector<Share> shares(rows.size());

		if (total <= 0.0)
			return shares;

		// For each depth, where its next row begins and where the stage it
		// is part of ends.
		std::vector<double> next((size_t)deepest + 2, 0.0);
		std::vector<double> end((size_t)deepest + 2, 1.0);

		for (size_t at = 0; at < rows.size(); at++)
		{
			const size_t depth = (size_t)rows[at].depth;
			const double from = next[depth];
			const double to = (std::min)(from + rows[at].average / total, end[depth]);

			shares[at] = { from, to };
			next[depth] = to;
			next[depth + 1] = from;
			end[depth + 1] = to;
		};

		return shares;
	};

	void StageTiming::Lanes()
	{
		const std::vector<Profiler::Thread>& threads = Profiler::Threads();

		if (threads.empty())
			return;

		// A lane for each depth, a little short of a line so lanes stand apart.
		constexpr float lane_lines = 1.1f;
		constexpr float lane_gap = 1.0f;
		constexpr float label_padding = 2.0f;

		const float lane = ImGui::GetTextLineHeight() * lane_lines;
		float label_width = 0.0f;

		for (const Profiler::Thread& thread : threads)
			label_width = (std::max)(label_width, ImGui::CalcTextSize(thread.name.c_str()).x + ImGui::GetStyle().ItemSpacing.x);

		const ImVec2 mouse = ImGui::GetIO().MousePos;
		ImDrawList* const draw = ImGui::GetWindowDrawList();

		ImGui::TextDisabled("%s", AverageFrame(Profiler::FrameLength()).c_str());

		for (const Profiler::Thread& thread : threads)
		{
			int deepest = 0;

			for (const Profiler::Row& row : thread.rows)
				deepest = (std::max)(deepest, row.depth);

			const ImVec2 corner = ImGui::GetCursorScreenPos();
			const float left = corner.x + label_width;
			const float width = (std::max)(ImGui::GetContentRegionAvail().x - label_width, stage_narrowest_bar);

			ImGui::PushID(thread.name.c_str());
			ImGui::TextUnformatted(thread.name.c_str());
			ImGui::SetCursorScreenPos(ImVec2(left, corner.y));
			ImGui::InvisibleButton("##lanes", ImVec2(width, lane * (float)(deepest + 1)));
			ImGui::PopID();

			const bool hovered = ImGui::IsItemHovered();
			const std::vector<Share> shares = Shares(thread.rows);
			int under = -1;

			for (size_t at = 0; at < thread.rows.size(); at++)
			{
				const Profiler::Row& row = thread.rows[at];

				if (row.average <= 0.0)
					continue;

				const float from = left + width * (float)shares[at].from;
				const float to = (std::max)(from + stage_narrowest_bar, left + width * (float)shares[at].to);
				const ImVec2 low(from, corner.y + lane * (float)row.depth);
				const ImVec2 high(to, low.y + lane - lane_gap);

				draw->AddRectFilled(low, high, StageColour(row.name));

				if (ImGui::CalcTextSize(row.name.c_str()).x + label_padding * 2.0f <= to - from)
					draw->AddText(ImVec2(from + label_padding, low.y), IM_COL32_BLACK, row.name.c_str());

				// A part is drawn after its stage, so the deepest under the mouse wins.
				if (hovered && mouse.x >= low.x && mouse.x < high.x && mouse.y >= low.y && mouse.y < high.y)
					under = (int)at;
			};

			if (under < 0)
				continue;

			constexpr double percent = 100.0;

			const Profiler::Row& row = thread.rows[under];
			const Share& share = shares[under];

			// Its share of the thread, in the colour of its bar.
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(row.name.c_str());
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(StageColour(row.name)), "%.1f%%", percent * (share.to - share.from));
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::Text(" of %s, %.3f ms average, %.3f ms peak", thread.name.c_str(), row.average, row.peak);
			ImGui::EndTooltip();
		};
	};

	void StageTiming::Details()
	{
		const std::vector<Profiler::Thread>& threads = Profiler::Threads();

		if (threads.empty())
			return;

		Lanes();

		const double frame = Profiler::FrameLength();
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		ImDrawList* const draw = ImGui::GetWindowDrawList();

		// Each thread's stages stacked a point every Bucket frames, the oldest
		// on the left.
		constexpr float graph_lines = 6.0f;
		constexpr int graph_frames = Profiler::Trend * Profiler::Bucket;
		constexpr double milliseconds_per_second = 1000.0;

		const double seconds_per_point = frame * (double)Profiler::Bucket / milliseconds_per_second;

		ImGui::TextDisabled("Milliseconds a frame over the last %d, about %.0f s", graph_frames, seconds_per_point * (double)Profiler::Trend);

		// Stage, Average and Peak.
		constexpr int columns = 3;

		for (const Profiler::Thread& thread : threads)
		{
			ImGui::PushID(thread.name.c_str());

			if (!ImGui::CollapsingHeader(thread.name.c_str(), &thread == &threads.front() ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
			{
				ImGui::PopID();
				continue;
			};

			std::array<double, Profiler::Trend> below{ };
			double highest = 0.0;

			for (const Profiler::Row& row : thread.rows)
				if (row.depth == 0)
					for (int age = 0; age < Profiler::Trend; age++)
						below[age] += row.trend[age];

			for (const double total : below)
				highest = (std::max)(highest, total);

			below.fill(0.0);

			ImGui::TextDisabled("Up to %.2f ms", highest);

			const ImVec2 corner = ImGui::GetCursorScreenPos();
			const ImVec2 size((std::max)(ImGui::GetContentRegionAvail().x, stage_narrowest_bar), ImGui::GetTextLineHeight() * graph_lines);
			const float step = size.x / (float)(Profiler::Trend - 1);

			ImGui::InvisibleButton("##graph", size);

			const int pointed = ImGui::IsItemHovered() ? std::clamp((int)std::lround((mouse.x - corner.x) / step), 0, Profiler::Trend - 1) : -1;
			const auto height = [&](double length) { return corner.y + size.y - size.y * (float)(length / highest); };
			const Profiler::Row* under = nullptr;

			draw->AddRectFilled(corner, ImVec2(corner.x + size.x, corner.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg));

			// Antialiased edges would show the background through the seams
			// between one point's quad and the next.
			const ImDrawListFlags flags = draw->Flags;
			draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;

			for (const Profiler::Row& row : thread.rows)
			{
				if (row.depth != 0 || highest <= 0.0)
					continue;

				const ImU32 fill = StageColour(row.name);

				for (int age = 0; age + 1 < Profiler::Trend; age++)
				{
					const float from = corner.x + step * (float)age;
					const float to = from + step;

					draw->AddQuadFilled(
						ImVec2(from, height(below[age] + row.trend[age])),
						ImVec2(to, height(below[age + 1] + row.trend[age + 1])),
						ImVec2(to, height(below[age + 1])),
						ImVec2(from, height(below[age])),
						fill);
				};

				if (pointed >= 0 && mouse.y <= height(below[pointed]) && mouse.y >= height(below[pointed] + row.trend[pointed]))
					under = &row;

				for (int age = 0; age < Profiler::Trend; age++)
					below[age] += row.trend[age];
			};

			draw->Flags = flags;

			if (under)
				ImGui::SetTooltip("%s: %.3f ms, about %.1f s ago", under->name.c_str(), under->trend[pointed], seconds_per_point * (double)(Profiler::Trend - 1 - pointed));

			if (!ImGui::BeginTable("##stages", columns, ImGuiTableFlags_RowBg))
			{
				ImGui::PopID();
				continue;
			};

			ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Average", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Peak", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			const float swatch = ImGui::GetTextLineHeight();

			for (const Profiler::Row& row : thread.rows)
			{
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetStyle().IndentSpacing * (float)row.depth);

				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + swatch, at.y + swatch), StageColour(row.name));
				ImGui::Dummy(ImVec2(swatch, swatch));
				ImGui::SameLine();
				ImGui::TextUnformatted(row.name.c_str());

				ImGui::TableNextColumn();
				ImGui::Text("%.2f", row.average);

				ImGui::TableNextColumn();
				ImGui::Text("%.2f", row.peak);
			};

			ImGui::EndTable();
			ImGui::PopID();
		};
	};

	void StageTiming::Overlay()
	{
		// How far in from the corner it sits, how much of the width it takes,
		// and how much of the game shows through.
		constexpr float inset = 8.0f;
		constexpr float width_share = 0.4f;
		constexpr float opacity = 0.85f;

		// The longest a frame's length runs to in the header, so a slow frame
		// widens nothing.
		constexpr double widest_frame = 999.99;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		// A height of none fits it to what it holds, frame by frame.
		ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + inset, viewport->WorkPos.y + inset), ImGuiCond_FirstUseEver);
		const float header_width = ImGui::CalcTextSize(AverageFrame(widest_frame).c_str()).x + ImGui::GetStyle().WindowPadding.x * 2.0f;

		ImGui::SetNextWindowSize(ImVec2((std::max)(viewport->WorkSize.x * width_share, header_width), 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(opacity);

		// A window sizing itself on its first frame draws even collapsed.
		const bool open =
			ImGui::Begin("Stage timing", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing) &&
			!ImGui::IsWindowCollapsed();

		Profiler::SetRecording(open);

		if (open)
			Lanes();

		ImGui::End();
	};
};

#endif
