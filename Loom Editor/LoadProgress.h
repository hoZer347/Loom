#pragma once

#include <iosfwd>
#include <optional>


namespace Loom
{
	/**
	* Loom::BuildTimings
	* - How long a script library's last build and last rebuild took, kept
	*   beside it between sessions as a key=seconds line each
	* - A build under way is judged by the last of its kind, since MSBuild
	*   says nothing about how far it has got
	*/
	struct BuildTimings final
	{
		static constexpr const char* Extension = ".buildtimes";

		// A line that is not one of the two keys with a positive number of
		// seconds leaves its kind unknown.
		static BuildTimings Read(std::istream& in);
		void Write(std::ostream& out) const;

		std::optional<double> Of(bool rebuild) const;
		void Set(bool rebuild, double seconds);

		// How far a build is after elapsed seconds, against how long the last
		// of its kind took, held short of the end until it is done. None with
		// nothing to go by.
		static std::optional<float> Progress(double elapsed, std::optional<double> expected);

	private:
		std::optional<double> m_build{ };
		std::optional<double> m_rebuild{ };
	};

	/**
	* Loom::ProjectLoad
	* - A project on its way in, under one bar: its scripts building, then
	*   the scene it starts on opening, a share each
	*/
	struct ProjectLoad final
	{
		bool compiles = false;
		bool opens = false;

		// The whole bar, from how far the step under way has got.
		float Fraction(bool opening, float step) const;
	};
};
