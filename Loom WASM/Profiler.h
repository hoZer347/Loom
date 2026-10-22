#pragma once

#include "Loom API.h"

#include <string>
#include <vector>


// Times the rest of the block as a stage of the frame on the thread it runs
// on. Nothing in a release build, where name is not even evaluated.
#ifdef NDEBUG
#define LOOM_STAGE(name)
#else
#define LOOM_STAGE_JOIN(a, b) a##b
#define LOOM_STAGE_ON(name, line) const Loom::Profiler::Stage LOOM_STAGE_JOIN(loom_stage_, line){ name }
#define LOOM_STAGE(name) LOOM_STAGE_ON(name, __LINE__)
#endif


namespace Loom
{
	/**
	* Loom::Profiler
	* - How long each stage of the frame took on each thread that worked on
	*   it, in debug builds only: LOOM_STAGE is nothing in a release build, so
	*   nothing is recorded there and Threads stays empty
	* - A stage runs from its LOOM_STAGE to the end of the block, and the
	*   stages begun inside it are its parts
	* - The share of a Jobs::For each worker takes counts, on that worker,
	*   towards the stage the For was started in
	* - The Engine ends each frame, which is when what every thread recorded
	*   is gathered, so a frame is read once it is over
	*/
	struct LOOM_API Profiler final
	{
		// The frames a Row reaches back over.
		static constexpr int Window = 60;

		// A stage as a thread ran it in the last frame, in milliseconds from
		// the frame's start.
		struct Span
		{
			std::string name;
			int depth;
			double start;
			double length;
		};

		// A stage as it has run on a thread over the last Window frames, in
		// milliseconds a frame: a frame it did not run in counts as none, and
		// one it ran in more than once as their sum.
		struct Row
		{
			std::string name;
			int depth;
			double average;
			double peak;
		};

		struct Thread
		{
			std::string name;

			// The last frame's, in the order they began.
			std::vector<Span> spans{ };

			// Every stage run within the window, each followed by its parts,
			// in the order they began in the last frame they ran in.
			std::vector<Row> rows{ };
		};

		// Opens a stage, which closes when it goes. name is copied as the
		// stage closes, so it need only last that long.
		struct LOOM_API Stage final
		{
			explicit Stage(const char* name);
			~Stage();

			Stage(const Stage&) = delete;
			Stage& operator=(const Stage&) = delete;

		private:
			double m_start;
		};

		// What the calling thread is listed as, and where: by order, then by
		// when each first recorded. One never named is listed last as
		// "Thread" and a number.
		static void NameThread(const std::string& name, int order);

		// The innermost stage open on the calling thread, or null.
		static const char* Current();

		// Gathers what every thread recorded since the last call as a frame.
		static void EndFrame();

		// The threads that ran a stage within the window, as of the last frame.
		static const std::vector<Thread>& Threads();

		// In milliseconds, from one EndFrame to the next.
		static double FrameLength();
	};
};
