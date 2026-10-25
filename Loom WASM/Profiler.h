#pragma once

#include "Loom API.h"

#include <array>
#include <string>
#include <vector>


// Times the rest of the block as a stage of the frame on the thread it runs
// on. Nothing in a final build, where name is not even evaluated.
#ifdef LOOM_FINAL
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
	*   it, while something is looking: a stage and the end of a frame cost
	*   a check and no more until SetRecording turns it on, and turning it
	*   off forgets it all
	* - Not in a final build, where LOOM_STAGE is nothing and what the
	*   engine calls does nothing
	* - A stage runs from its LOOM_STAGE to the end of the block, and the
	*   stages begun inside it are its parts
	* - The share of a Jobs::For each worker takes counts, on that worker,
	*   towards the stage the For was started in
	* - The Engine ends each frame, which is when what every thread recorded
	*   is gathered, so a frame is read once it is over
	* - Unless SetEveryFrame, a frame is recorded only once a poll is due,
	*   Rate times a second, so the window spans the same time at any
	*   frame rate. The frames between record nothing
	*/
	struct LOOM_API Profiler final
	{
		// The recorded frames all of a Row but its trend reaches back over.
		static constexpr int Window = 100;

		// The recorded frames each point of a Row's trend is the average of.
		static constexpr int Bucket = 10;

		// The points a Row's trend holds, so it reaches back Trend * Bucket
		// recorded frames.
		static constexpr int Trend = 300;

		// The polls a second, the most frames recorded unless SetEveryFrame.
		static constexpr int Rate = 60;

		// A stage as it has run on a thread, in milliseconds a frame: a frame
		// it did not run in counts as none, and one it ran in more than once
		// as their sum. All but trend reach back over the last Window frames.
		struct Row
		{
			std::string name;
			int depth;
			double average;
			double peak;

			// Each frame's, oldest first.
			std::array<double, Window> history;

			// Each whole Bucket's average, oldest first.
			std::array<double, Trend> trend;
		};

		// What one thread ran over the trend.
		struct Thread
		{
			std::string name;

			// Every stage run within the trend, each followed by its parts,
			// in the order they began in the last frame they ran in.
			std::vector<Row> rows{ };
		};

#ifdef LOOM_FINAL
		static void NameThread(const std::string&, int) { };
		static const char* Current() { return nullptr; };
		static void EndFrame() { };
#else
		// Opens a stage, which closes when it goes. name is copied as the
		// stage closes, so it need only last that long.
		struct LOOM_API Stage final
		{
			explicit Stage(const char* name);
			~Stage();

			Stage(const Stage&) = delete;
			Stage& operator=(const Stage&) = delete;

		private:
			double m_start = 0.0;

			// Whether recording was on as it opened, so a stage open across
			// SetRecording still closes as it began. One still open as it
			// stops is not recorded.
			bool m_open;
		};

		// Off until turned on. The frame that ends after turning it on is the
		// first recorded, and turning it off forgets what was.
		static void SetRecording(bool recording);
		static bool Recording();

		// Off until turned on, recording each frame rather than one a poll.
		static void SetEveryFrame(bool every);
		static bool EveryFrame();

		// What the calling thread is listed as, and where: by order, then by
		// when each first recorded. One never named is listed last as
		// "Thread" and a number.
		static void NameThread(const std::string& name, int order);

		// The innermost stage open on the calling thread, or null.
		static const char* Current();

		// Gathers what every thread recorded since the last call as a frame.
		static void EndFrame();

		// The threads that ran a stage within the trend, as of the last frame.
		static const std::vector<Thread>& Threads();

		// In milliseconds from one EndFrame to the next, averaged over the
		// window.
		static double FrameLength();

		// In milliseconds from the start of one recorded frame to the next,
		// averaged over the window.
		static double Spacing();
#endif
	};
};
