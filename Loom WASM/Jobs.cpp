#include "Jobs.h"

#include "Profiler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif


namespace Loom
{
	namespace
	{
		#pragma region Waiting

		// How long a worker waits awake for the next job before sleeping: a
		// physics step runs several back to back, and waking a sleeping thread
		// takes longer than any of them. It looks at the clock only every so
		// many spins.
		constexpr std::chrono::microseconds STAY_AWAKE(200);
		constexpr unsigned CLOCK_EVERY = 64;

		// How long, in spins, a thread waiting on others keeps its core before
		// it sleeps until woken: about as long as a short piece of work, and
		// no longer, so one it waits on that has lost its core gets this one.
		constexpr unsigned MOST_SPINS = 1024;

		// Waits a moment on a spinning thread, without handing its core to
		// another thread for a whole time slice as a yield may.
		void Relax()
		{
#if defined(_M_X64) || defined(__x86_64__)
			_mm_pause();
#endif
		};

		// Until ready says so, spinning a while and then sleeping on value,
		// which whatever makes it ready changes and notifies.
		template <typename T, typename Ready>
		void Await(const std::atomic<T>& value, const Ready& ready)
		{
			for (unsigned spins = 0; spins < MOST_SPINS; spins++)
			{
				if (ready())
					return;

				Relax();
			};

			for (T seen = value.load(); !ready(); seen = value.load())
				value.wait(seen);
		};

		#pragma endregion

		#pragma region Jobs

		// What a worker's share of a For started outside any stage counts as.
		constexpr const char* UNSTAGED = "Jobs";

		struct Job
		{
			const Jobs::Work* work;
			const char* stage;
			size_t count;
			size_t grain;
			size_t chunks;

			std::atomic<size_t> next{ 0 };
			std::atomic<size_t> done{ 0 };
		};

		// Takes chunks until none are left.
		void Help(Job& job)
		{
			for (size_t chunk = job.next.fetch_add(1); chunk < job.chunks; chunk = job.next.fetch_add(1))
			{
				const size_t begin = chunk * job.grain;
				(*job.work)(chunk, begin, std::min(begin + job.grain, job.count));

				job.done.fetch_add(1);
				job.done.notify_all();
			};
		};

		#pragma endregion

		#pragma region Pool

		// The machine's cores, rather than the threads they run at once: two
		// threads on one core share its units, so a second that only waits
		// slows the first, and one that works gains little. A browser tells
		// only the threads, as many as the page starts with.
		unsigned MachineCores()
		{
#if defined(_WIN32)
			DWORD length = 0;
			GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);

			std::vector<char> buffer(length);
			auto* information = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());

			if (GetLogicalProcessorInformationEx(RelationProcessorCore, information, &length))
			{
				unsigned cores = 0;

				for (DWORD at = 0; at < length; at += reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + at)->Size)
					cores++;

				return cores;
			};
#endif

			return std::max(1u, std::thread::hardware_concurrency());
		};

		struct Pool
		{
			Pool()
			{
				const unsigned cores = MachineCores();

				for (unsigned i = 1; i < cores; i++)
					workers.emplace_back([this]() { Run(++started); });
			};

			~Pool()
			{
				stopping = true;
				generation++;
				generation.notify_all();

				for (std::thread& worker : workers)
					worker.join();
			};

			// number counts the workers in the order they started, from one.
			void Run(unsigned number)
			{
				Profiler::NameThread("Worker " + std::to_string(number), (int)number);

				unsigned seen = 0;

				for (;;)
				{
					// Awake for a while first, for the next of a run of jobs.
					const auto until = std::chrono::steady_clock::now() + STAY_AWAKE;

					for (unsigned spins = 1; generation == seen; spins++)
					{
						if (spins % CLOCK_EVERY == 0 && std::chrono::steady_clock::now() >= until)
						{
							generation.wait(seen);
							break;
						};

						Relax();
					};

					if (stopping)
						return;

					seen = generation;

					// Counted in before it looks, so the caller, having taken
					// its job down, waits for any that found it. One past the
					// limit stays out.
					helping++;

					if (Job* job = current; job && number < Jobs::Threads())
					{
						LOOM_STAGE(job->stage);
						Help(*job);
					};

					helping--;
					helping.notify_all();
				};
			};

			std::vector<std::thread> workers{ };

			// Workers running, and how many threads may share a For; none for
			// no limit.
			std::atomic<unsigned> started{ 0 };
			std::atomic<unsigned> most{ 0 };

			std::atomic<unsigned> generation{ 0 };
			std::atomic<Job*> current{ nullptr };
			std::atomic<unsigned> helping{ 0 };
			std::atomic<bool> stopping{ false };

			// Held by the For the workers are on, so one from another thread
			// waits its turn.
			std::mutex taking{ };
		};

		Pool& Workers()
		{
			static Pool pool;
			return pool;
		};

		#pragma endregion
	};

	#pragma region Work

	void Jobs::For(size_t count, size_t grain, const Work& work)
	{
		if (grain == 0)
			grain = 1;

		const size_t chunks = Chunks(count, grain);
		Pool& pool = Workers();

		if (chunks <= 1 || Threads() == 1)
		{
			for (size_t chunk = 0; chunk < chunks; chunk++)
				work(chunk, chunk * grain, std::min((chunk + 1) * grain, count));

			return;
		};

		const std::lock_guard<std::mutex> taken(pool.taking);
		const char* const stage = Profiler::Current();

		Job job;
		job.work = &work;
		job.stage = stage ? stage : UNSTAGED;
		job.count = count;
		job.grain = grain;
		job.chunks = chunks;

		pool.current = &job;
		pool.generation++;
		pool.generation.notify_all();

		Help(job);

		Await(job.done, [&]() { return job.done == chunks; });

		// The job, on this stack, outlives any worker still inside it.
		pool.current = nullptr;
		Await(pool.helping, [&]() { return pool.helping == 0; });
	};

	void Jobs::WaitFor(const std::atomic<int>& counter, int value)
	{
		Await(counter, [&]() { return counter.load(std::memory_order_acquire) >= value; });
	};

	#pragma endregion

	#pragma region Threads

	unsigned Jobs::Cores()
	{
		return (unsigned)Workers().workers.size() + 1;
	};

	unsigned Jobs::Threads()
	{
		const Pool& pool = Workers();
		const unsigned running = pool.started + 1;

		return pool.most == 0 ? running : std::min(running, pool.most.load());
	};

	void Jobs::SetMostThreads(unsigned most)
	{
		Workers().most = most;
	};

	#pragma endregion
};
