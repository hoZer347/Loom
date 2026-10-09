#include "Jobs.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>


namespace Loom
{
	namespace
	{
		// How long a worker waits awake for the next job before sleeping: a
		// physics step runs several back to back, and waking a sleeping thread
		// takes longer than any of them.
		constexpr std::chrono::microseconds STAY_AWAKE(200);

		struct Job
		{
			const Jobs::Work* work;
			size_t count;
			size_t grain;
			size_t chunks;

			std::atomic<size_t> next{ 0 };
			std::atomic<size_t> done{ 0 };

			// Workers inside Help, which the caller waits out before the job,
			// on its stack, goes away.
			std::atomic<unsigned> helping{ 0 };
		};

		// Takes chunks until none are left.
		void Help(Job& job)
		{
			for (size_t chunk = job.next.fetch_add(1); chunk < job.chunks; chunk = job.next.fetch_add(1))
			{
				const size_t begin = chunk * job.grain;
				(*job.work)(chunk, begin, std::min(begin + job.grain, job.count));
				job.done.fetch_add(1, std::memory_order_release);
			};
		};

		struct Pool
		{
			Pool()
			{
				const unsigned machine = std::thread::hardware_concurrency();

				for (unsigned i = 1; i < machine; i++)
					workers.emplace_back([this]() { Run(); });
			};

			~Pool()
			{
				{
					std::lock_guard<std::mutex> lock(mutex);
					stopping = true;
				};

				wake.notify_all();

				for (std::thread& worker : workers)
					worker.join();
			};

			void Run()
			{
				unsigned seen = 0;

				for (;;)
				{
					// Awake for a while first, for the next of a run of jobs.
					const auto until = std::chrono::steady_clock::now() + STAY_AWAKE;

					while (generation.load(std::memory_order_acquire) == seen && std::chrono::steady_clock::now() < until)
						std::this_thread::yield();

					Job* job = nullptr;

					{
						std::unique_lock<std::mutex> lock(mutex);
						wake.wait(lock, [&]() { return stopping || generation.load() != seen; });

						if (stopping)
							return;

						seen = generation.load();
						job = current;

						if (job)
							job->helping++;
					};

					if (job)
					{
						Help(*job);
						job->helping--;
					};
				};
			};

			std::vector<std::thread> workers{ };

			std::mutex mutex{ };
			std::condition_variable wake{ };
			std::atomic<unsigned> generation{ 0 };
			Job* current = nullptr;
			bool stopping = false;
		};

		Pool& Workers()
		{
			static Pool pool;
			return pool;
		};
	};

	void Jobs::For(size_t count, size_t grain, const Work& work)
	{
		const size_t chunks = Chunks(count, grain);
		Pool& pool = Workers();

		if (chunks <= 1 || pool.workers.empty())
		{
			for (size_t chunk = 0; chunk < chunks; chunk++)
				work(chunk, chunk * grain, std::min((chunk + 1) * grain, count));

			return;
		};

		Job job;
		job.work = &work;
		job.count = count;
		job.grain = grain;
		job.chunks = chunks;

		{
			std::lock_guard<std::mutex> lock(pool.mutex);
			pool.current = &job;
			pool.generation++;
		};

		pool.wake.notify_all();

		Help(job);

		while (job.done.load(std::memory_order_acquire) < chunks)
			std::this_thread::yield();

		{
			std::lock_guard<std::mutex> lock(pool.mutex);
			pool.current = nullptr;
		};

		while (job.helping.load() > 0)
			std::this_thread::yield();
	};

	unsigned Jobs::Threads()
	{
		return (unsigned)Workers().workers.size() + 1;
	};
};
