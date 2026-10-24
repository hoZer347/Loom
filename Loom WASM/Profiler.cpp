#include "Profiler.h"

#ifndef LOOM_FINAL

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>


namespace Loom
{
	namespace
	{
		// Past this many stages in a frame a thread's are dropped, so one
		// recording with no frames ending, such as a test's, stays bounded.
		constexpr size_t MOST_SPANS = 4096;

		// A frame's start for a stage that did not run in it.
		constexpr double NOT_RUN = -1.0;

		// Where a thread never named is listed, and what it is called.
		constexpr int UNNAMED_ORDER = std::numeric_limits<int>::max();
		constexpr const char* UNNAMED = "Thread ";

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
			// Moves the parts to the order they begin in this frame, ahead of
			// those that have not run in it.
			Node& Part(const std::string& name, unsigned long long frame, double start)
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
					part.starts.fill(NOT_RUN);
				};

				std::rotate(parts.begin() + placed, parts.begin() + at, parts.begin() + at + 1);

				Node& part = parts[placed++];
				part.last = frame;
				part.began = start;

				return part;
			};

			std::string name{ };

			// Each frame's sum, the slot a frame's number picks overwritten
			// as the window moves on.
			std::array<double, Profiler::Window> history{ };
			double sum = 0.0;

			// Each frame's first start, from the frame's, or NOT_RUN.
			std::array<double, Profiler::Window> starts{ };
			double began = 0.0;

			// Each whole Bucket's average, the slot a bucket's number picks
			// overwritten as the trend moves on, and the sum of this one so far.
			std::array<double, Profiler::Trend> trend{ };
			double gathered = 0.0;

			unsigned long long first = 0;
			unsigned long long last = 0;

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

			std::vector<Profiler::Thread> threads{ };
		};

		// Never destroyed, since a worker may still be recording as the
		// program ends.
		Registry& Shared()
		{
			static Registry& registry = *new Registry;
			return registry;
		};

		std::atomic<bool> s_recording{ false };

		thread_local Log* t_log = nullptr;
		thread_local std::vector<const char*> t_open{ };

		double Now()
		{
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Shared().origin).count();
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

		// Takes this frame's sums into the window and the trend, and forgets
		// the parts that have not run within the trend.
		void Close(Node& node, unsigned long long frame)
		{
			const unsigned long long buckets = (frame + 1) / Profiler::Bucket;

			node.history[frame % Profiler::Window] = node.sum;
			node.starts[frame % Profiler::Window] = node.last == frame ? node.began : NOT_RUN;
			node.gathered += node.sum;
			node.sum = 0.0;
			node.placed = 0;

			if ((frame + 1) % Profiler::Bucket == 0)
			{
				node.trend[(buckets - 1) % Profiler::Trend] = node.gathered / (double)Profiler::Bucket;
				node.gathered = 0.0;
			};

			std::erase_if(node.parts, [buckets](const Node& part) { return part.last / Profiler::Bucket + Profiler::Trend < buckets; });

			for (Node& part : node.parts)
				Close(part, frame);
		};

		void List(const Node& node, unsigned long long frame, int depth, std::vector<Profiler::Row>& rows)
		{
			const unsigned long long buckets = (frame + 1) / Profiler::Bucket;

			for (const Node& part : node.parts)
			{
				const unsigned long long counted = std::min<unsigned long long>(frame - part.first + 1, Profiler::Window);
				Profiler::Row& row = rows.emplace_back(Profiler::Row{ part.name, depth, 0.0, 0.0, 0.0, { }, { } });
				double began = 0.0;
				int ran = 0;

				for (int age = 0; age < Profiler::Window; age++)
				{
					const size_t slot = (size_t)((frame + 1 + age) % Profiler::Window);

					row.history[age] = part.history[slot];
					row.average += part.history[slot];
					row.peak = std::max(row.peak, part.history[slot]);

					if (part.starts[slot] != NOT_RUN)
					{
						began += part.starts[slot];
						ran++;
					};
				};

				for (int point = 0; point < Profiler::Trend; point++)
					row.trend[point] = part.trend[(buckets + point) % Profiler::Trend];

				row.average /= (double)counted;
				row.start = began / (double)std::max(ran, 1);

				List(part, frame, depth + 1, rows);
			};
		};
	};

	Profiler::Stage::Stage(const char* name) :
		m_open(s_recording.load(std::memory_order_relaxed))
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

	void Profiler::SetRecording(bool recording)
	{
		if (s_recording.exchange(recording, std::memory_order_relaxed) == recording)
			return;

		Registry& shared = Shared();
		std::lock_guard<std::mutex> lock(shared.mutex);

		// A stage that saw recording on as it went off may close into its
		// log after, so each is emptied as recording starts again.
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
			return;
		};

		shared.threads.clear();
		shared.frameLengths.fill(0.0);
		shared.frameLength = 0.0;
		shared.frame = 0;
	};

	bool Profiler::Recording()
	{
		return s_recording.load(std::memory_order_relaxed);
	};

	const char* Profiler::Current()
	{
		return t_open.empty() ? nullptr : t_open.back();
	};

	void Profiler::EndFrame()
	{
		if (!Recording())
			return;

		Registry& shared = Shared();
		const double now = Now();

		std::lock_guard<std::mutex> lock(shared.mutex);

		shared.frameLengths[shared.frame % Window] = now - shared.frameStart;
		shared.frameLength = 0.0;

		for (const double length : shared.frameLengths)
			shared.frameLength += length;

		shared.frameLength /= (double)std::min<unsigned long long>(shared.frame + 1, Window);
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

			// A part begins no sooner than its stage, and on the same tick
			// only after it.
			std::sort(recorded.begin(), recorded.end(),
				[](const Recorded& a, const Recorded& b) { return a.start != b.start ? a.start < b.start : a.depth < b.depth; });

			open.clear();

			for (const Recorded& span : recorded)
			{
				// A part whose stage was dropped goes under the deepest still open.
				const size_t depth = std::min((size_t)span.depth, open.size());
				const double length = span.end - span.start;

				open.resize(depth);

				Node& node = (depth == 0 ? log->root : *open.back()).Part(span.name, shared.frame, span.start - shared.frameStart);
				node.sum += length;
				open.push_back(&node);
			};

			Close(log->root, shared.frame);
			List(log->root, shared.frame, 0, thread.rows);

			if (!thread.rows.empty())
				listed.emplace_back(order, std::move(thread));
		};

		std::stable_sort(listed.begin(), listed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

		for (auto& [order, thread] : listed)
			shared.threads.push_back(std::move(thread));

		shared.frameStart = now;
		shared.frame++;
	};

	const std::vector<Profiler::Thread>& Profiler::Threads()
	{
		return Shared().threads;
	};

	double Profiler::FrameLength()
	{
		return Shared().frameLength;
	};
};
#endif
