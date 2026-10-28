#pragma once

#include <filesystem>
#include <string>
#include <thread>


namespace Loom
{
	/**
	* Loom::VisualStudio
	* - Opens a file in the Visual Studio that has its solution, starting one
	*   on the solution when none has
	* - Explorer sends headers here, so double-clicking one opens its project
	*/
	struct VisualStudio final
	{
		// The files Explorer opens through the editor in Visual Studio.
		static constexpr const char* header_extension = ".hpp";

		// The first path that exists among vswhere's answers for the newest
		// Visual Studio, empty when there is none.
		static std::string Where(const std::string& arguments);

		// devenv.exe from the newest Visual Studio on this machine, or empty
		// when there is none. Looked up once and held.
		static const std::string& Find();

		// The first solution in the nearest folder above file that has one,
		// empty when none does.
		static std::string SolutionFor(const std::filesystem::path& file);

		// Opens file at line, or where Visual Studio puts the caret for a line
		// of zero. A Visual Studio started on solution only takes the file once
		// it has loaded, which the returned thread waits for; empty when there
		// is nothing to wait for.
		static std::thread Open(const std::string& solution, const std::string& file, int line = 0, int column = 0);
	};
};
