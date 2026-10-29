#include "Profiler.h"

#ifndef LOOM_FINAL

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>


namespace Loom
{
	namespace
	{
		#pragma region Constants

		// Caps a thread's stages per frame, so a recording with no frame ends stays bounded.
		constexpr size_t MOST_SPANS = 4096;

		// In milliseconds from one poll to the next.
		constexpr double POLL = 1000.0 / (double)Profiler::Rate;

		// In milliseconds, each point of the trend's share of a second.
		constexpr double POINT = 1000.0 / (double)Profiler::Points;

		// Polls step by the golden ratio, so fixed-cadence work is not always caught or missed.
		constexpr double SHORTEST_POLL = 0.5;
		constexpr double POLL_STEP = 0.618034;

		// Where a thread never named is listed, and what it is called.
		constexpr int UNNAMED_ORDER = std::numeric_limits<int>::max();
		constexpr const char* UNNAMED = "Thread ";

		#pragma endregion

		#pragma region Storage

		struct Recorded
		{
			std::string name;
			int depth;
			double start;
			double end;
		};

		// A stage and its parts as they have run on one thread.
		struct Node
		{
			// Reorders parts to their start order this frame.
			Node& Part(const std::string& name, unsigned long long frame, unsigned long long point)
			{
				const auto found = std::find_if(parts.begin(), parts.end(), [&name](const Node& part) { return part.name == name; });
				const size_t at = (size_t)(found - parts.begin());

				if (at < placed)
					return *found;

				if (found == parts.end())
				{
					Node& part = parts.emplace_back();
					part.name = name;
					part.first = frame;
				};

				std::rotate(parts.begin() + placed, parts.begin() + at, parts.begin() + at + 1);

				Node& part = parts[placed++];
				part.last = frame;
				part.lastPoint = point;

				return part;
			};

			std::string name{ };

			// Each frame's sum, in a ring the frame number indexes.
			std::array<double, Profiler::Window> history{ };
			double sum = 0.0;

			// Each point's average in a ring the point number indexes, and the next one's running sum.
			std::array<double, Profiler::Trend> trend{ };
			double gathered = 0.0;

			unsigned long long first = 0;
			unsigned long long last = 0;
			unsigned long long lastPoint = 0;

			std::vector<Node> parts{ };

			// How many parts have begun this frame.
			size_t placed = 0;
		};

		struct Log
		{
			std::mutex mutex{ };
			std::string name{ };
			int order = UNNAMED_ORDER;
			std::vector<Recorded> recorded{ };

			// EndFrame's alone.
			Node root{ };
		};

		struct Registry
		{
			std::mutex mutex{ };
			std::vector<std::unique_ptr<Log>> logs{ };

			const std::chrono::steady_clock::time_point origin = std::chrono::steady_clock::now();
			double frameStart = 0.0;
			std::array<double, Profiler::Window> frameLengths{ };
			double frameLength = 0.0;
			unsigned long long frame = 0;

			// The next poll and how many are due.
			double due = 0.0;
			unsigned long long polls = 0;

			// The trend's points closed so far, when the next closes, and the frames recorded towards it.
			unsigned long long points = 0;
			double pointEnd = 0.0;
			int pointFrames = 0;

			std::vector<Profiler::Thread> threads{ };
		};

		// Never destroyed, since a worker may record as the program ends.
		Registry& Shared()
		{
			static Registry& registry = *new Registry;
			return registry;
		};

		std::atomic<bool> s_recording{ false };
		std::atomic<bool> s_everyFrame{ false };

		// Whether the frame running now is recorded.
		std::atomic<bool> s_polled{ false };

		// What Advance has added to the clock.
		std::atomic<double> s_advanced{ 0.0 };

		thread_local Log* t_log = nullptr;
		thread_local std::vector<const char*> t_open{ };

		double Now()
		{
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Shared().origin).count() + s_advanced.load(std::memory_order_relaxed);
		};

		Log& ThisThread()
		{
			if (t_log)
				return *t_log;

			Registry& shared = Shared();
			std::lock_guard<std::mutex> lock(shared.mutex);

			t_log = shared.logs.emplace_back(std::make_unique<Log>()).get();
			t_log->name = UNNAMED + std::to_string(shared.logs.size());

			return *t_log;
		};

		#pragma endregion

		#pragma region Frames

		// Polls keep their schedule unless a frame ran past a whole poll.
		void Poll(Registry& shared, double now)
		{
			const bool due = now >= shared.due;

			if (due)
			{
				const double share = SHORTEST_POLL + std::fmod((double)shared.polls++ * POLL_STEP, 1.0);
				shared.due = std::max(shared.due + POLL * share, now);
			};

			const bool next = due || s_everyFrame.load(std::memory_order_relaxed);

			if (next)
				shared.frameStart = now;

			s_polled.store(next, std::memory_order_relaxed);
		};

		// Folds this frame into the window and the trend's next point.
		void Fold(Node& node, unsigned long long frame)
		{
			node.history[frame % Profiler::Window] = node.sum;
			node.gathered += node.sum;
			node.sum = 0.0;
			node.placed = 0;

			for (Node& part : node.parts)
				Fold(part, frame);
		};

		// Sets the last closing points before after to their frames' average,
		// dropping parts idle for the trend.
		void Close(Node& node, unsigned long long after, int closing, int frames)
		{
			const double average = node.gathered / (double)frames;

			for (int point = 0; point < closing; point++)
				node.trend[(after - (unsigned long long)closing + (unsigned long long)point) % Profiler::Trend] = average;

			node.gathered = 0.0;

			std::erase_if(node.parts, [after](const Node& part) { return part.lastPoint + Profiler::Trend < after; });

			for (Node& part : node.parts)
				Close(part, after, closing, frames);
		};

		void List(const Node& node, unsigned long long frame, unsigned long long points, int depth, std::vector<Profiler::Row>& rows)
		{
			for (const Node& part : node.parts)
			{
				const unsigned long long counted = std::min<unsigned long long>(frame - part.first + 1, Profiler::Window);
				Profiler::Row& row = rows.emplace_back(Profiler::Row{ part.name, depth, 0.0, 0.0, { }, { } });

				for (int age = 0; age < Profiler::Window; age++)
				{
					const size_t slot = (size_t)((frame + 1 + age) % Profiler::Window);

					row.history[age] = part.history[slot];
					row.average += part.history[slot];
					row.peak = std::max(row.peak, part.history[slot]);
				};

				for (int point = 0; point < Profiler::Trend; point++)
					row.trend[point] = part.trend[(points + point) % Profiler::Trend];

				row.average /= (double)counted;

				List(part, frame, points, depth + 1, rows);
			};
		};

		#pragma endregion
	};

	#pragma region Stages

	Profiler::Stage::Stage(const char* name) :
		m_open(s_recording.load(std::memory_order_relaxed) && s_polled.load(std::memory_order_relaxed))
	{
		if (!m_open)
			return;

		m_start = Now();
		t_open.push_back(name);
	};

	Profiler::Stage::~Stage()
	{
		if (!m_open)
			return;

		const double end = Now();
		const char* const name = t_open.back();

		t_open.pop_back();

		if (!Recording())
			return;

		Log& log = ThisThread();
		std::lock_guard<std::mutex> lock(log.mutex);

		if (log.recorded.size() < MOST_SPANS)
			log.recorded.push_back({ name, (int)t_open.size(), m_start, end });
	};

	void Profiler::NameThread(const std::string& name, int order)
	{
		Log& log = ThisThread();
		std::lock_guard<std::mutex> lock(log.mutex);

		log.name = name;
		log.order = order;
	};

	const char* Profiler::Current()
	{
		return t_open.empty() ? nullptr : t_open.back();
	};

	#pragma endregion

	#pragma region Recording

	void Profiler::SetRecording(bool recording)
	{
		if (s_recording.exchange(recording, std::memory_order_relaxed) == recording)
			return;

		Registry& shared = Shared();
		std::lock_guard<std::mutex> lock(shared.mutex);

		// A stage may close into its log after recording stopped, so each is emptied.
		for (const std::unique_ptr<Log>& log : shared.logs)
		{
			std::lock_guard<std::mutex> named(log->mutex);
			log->recorded.clear();

			if (!recording)
				log->root = Node{ };
		};

		if (recording)
		{
			shared.frameStart = Now();
			shared.due = shared.frameStart + POLL;
			shared.pointEnd = shared.frameStart + POINT;
			shared.pointFrames = 0;
			s_polled.store(true, std::memory_order_relaxed);
			return;
		};

		shared.threads.clear();
		shared.frameLengths.fill(0.0);
		shared.frameLength = 0.0;
		shared.frame = 0;
		shared.points = 0;
	};

	bool Profiler::Recording()
	{
		return s_recording.load(std::memory_order_relaxed);
	};

	void Profiler::SetEveryFrame(bool every)
	{
		s_everyFrame.store(every, std::memory_order_relaxed);
	};

	bool Profiler::EveryFrame()
	{
		return s_everyFrame.load(std::memory_order_relaxed);
	};

	#pragma endregion

	#pragma region Frames

	void Profiler::EndFrame()
	{
		if (!Recording())
			return;

		Registry& shared = Shared();
		const double now = Now();

		std::lock_guard<std::mutex> lock(shared.mutex);

		// A stage of a recorded frame may close in one that is not.
		if (!s_polled.load(std::memory_order_relaxed))
		{
			for (const std::unique_ptr<Log>& log : shared.logs)
			{
				std::lock_guard<std::mutex> named(log->mutex);
				log->recorded.clear();
			};

			Poll(shared, now);
			return;
		};

		const unsigned long long counted = std::min<unsigned long long>(shared.frame + 1, Window);

		shared.frameLengths[shared.frame % Window] = now - shared.frameStart;
		shared.frameLength = 0.0;

		for (const double length : shared.frameLengths)
			shared.frameLength += length;

		shared.frameLength /= (double)counted;

		// A frame running past several points' ends stands for each of them.
		const int frames = ++shared.pointFrames;
		unsigned long long passed = 0;

		if (now >= shared.pointEnd)
		{
			passed = 1 + (unsigned long long)((now - shared.pointEnd) / POINT);
			shared.pointEnd += POINT * (double)passed;
			shared.pointFrames = 0;
		};

		const unsigned long long after = shared.points + passed;
		const int closing = (int)std::min<unsigned long long>(passed, Trend);

		shared.threads.clear();

		std::vector<Recorded> recorded{ };
		std::vector<Node*> open{ };
		std::vector<std::pair<int, Thread>> listed{ };

		for (const std::unique_ptr<Log>& log : shared.logs)
		{
			Thread thread{ };
			int order = UNNAMED_ORDER;

			recorded.clear();

			{
				std::lock_guard<std::mutex> named(log->mutex);

				recorded.swap(log->recorded);
				thread.name = log->name;
				order = log->order;
			};

			// A part begins no sooner than its stage, and after it on the same tick.
			std::sort(recorded.begin(), recorded.end(),
				[](const Recorded& a, const Recorded& b) { return a.start != b.start ? a.start < b.start : a.depth < b.depth; });

			open.clear();

			for (const Recorded& span : recorded)
			{
				// A part whose stage was dropped goes under the deepest still open.
				const size_t depth = std::min((size_t)span.depth, open.size());
				const double length = span.end - span.start;

				open.resize(depth);

				Node& node = (depth == 0 ? log->root : *open.back()).Part(span.name, shared.frame, shared.points);
				node.sum += length;
				open.push_back(&node);
			};

			Fold(log->root, shared.frame);

			if (closing > 0)
				Close(log->root, after, closing, frames);

			List(log->root, shared.frame, after, 0, thread.rows);

			if (!thread.rows.empty())
				listed.emplace_back(order, std::move(thread));
		};

		std::stable_sort(listed.begin(), listed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

		for (auto& [order, thread] : listed)
			shared.threads.push_back(std::move(thread));

		shared.frame++;
		shared.points = after;
		Poll(shared, now);
	};

	const std::vector<Profiler::Thread>& Profiler::Threads()
	{
		return Shared().threads;
	};

	double Profiler::FrameLength()
	{
		return Shared().frameLength;
	};

	double Profiler::TrendProgress()
	{
		return std::clamp(1.0 - (Shared().pointEnd - Now()) / POINT, 0.0, 1.0);
	};

	void Profiler::Advance(double milliseconds)
	{
		s_advanced.store(s_advanced.load(std::memory_order_relaxed) + milliseconds, std::memory_order_relaxed);
	};

	#pragma endregion
};
#endif
