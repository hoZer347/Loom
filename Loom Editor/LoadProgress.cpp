#include "LoadProgress.h"

#include <algorithm>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <string>


namespace Loom
{
	namespace
	{
		constexpr const char* BUILD_KEY = "build";
		constexpr const char* REBUILD_KEY = "rebuild";

		// Where a build that outlasts the last of its kind holds, rather than
		// looking done before it is.
		constexpr float PROGRESS_HOLD = 0.95f;
	};

	BuildTimings BuildTimings::Read(std::istream& in)
	{
		BuildTimings timings;
		std::string line;

		while (std::getline(in, line))
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
				line.pop_back();

			const size_t equals = line.find('=');

			if (equals == std::string::npos)
				continue;

			const std::string key = line.substr(0, equals);
			const char* const number = line.c_str() + equals + 1;

			char* end = nullptr;
			const double seconds = std::strtod(number, &end);

			if (end == number || *end != '\0' || !(seconds > 0.0))
				continue;

			if (key == BUILD_KEY)
				timings.m_build = seconds;
			else if (key == REBUILD_KEY)
				timings.m_rebuild = seconds;
		};

		return timings;
	};

	void BuildTimings::Write(std::ostream& out) const
	{
		if (m_build)
			out << BUILD_KEY << '=' << *m_build << '\n';

		if (m_rebuild)
			out << REBUILD_KEY << '=' << *m_rebuild << '\n';
	};

	std::optional<double> BuildTimings::Of(bool rebuild) const
	{
		return rebuild ? m_rebuild : m_build;
	};

	void BuildTimings::Set(bool rebuild, double seconds)
	{
		(rebuild ? m_rebuild : m_build) = seconds;
	};

	std::optional<float> BuildTimings::Progress(double elapsed, std::optional<double> expected)
	{
		if (!expected || !(*expected > 0.0))
			return std::nullopt;

		return (float)std::clamp(elapsed / *expected, 0.0, (double)PROGRESS_HOLD);
	};

	float ProjectLoad::Fraction(bool opening, float step) const
	{
		// A scene being opened is a step of its own even where the load did
		// not expect one.
		const int steps = (compiles ? 1 : 0) + (opens || opening ? 1 : 0);
		const int done = compiles && opening ? 1 : 0;

		return (done + step) / (float)(std::max)(steps, 1);
	};
};
