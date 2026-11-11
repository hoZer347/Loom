#include "doctest.h"

// Not in the web test build, which has no editor.
#ifndef __EMSCRIPTEN__

#include "VisualStudio.h"

#include <filesystem>
#include <fstream>


namespace
{
	// A project folder under the temp directory, gone again afterwards.
	struct Project
	{
		std::filesystem::path root;

		explicit Project(const std::string& name)
			: root(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(root);
			std::filesystem::create_directories(root / "Scripts" / "Units");
		};

		~Project()
		{
			std::error_code code;
			std::filesystem::remove_all(root, code);
		};

		std::filesystem::path Write(const std::filesystem::path& relative) const
		{
			std::ofstream(root / relative);
			return root / relative;
		};
	};
};

TEST_SUITE("VisualStudio")
{
	TEST_CASE("a header is told by its extension in any case")
	{
		CHECK(Loom::VisualStudio::IsHeader("Scripts/Tank.hpp"));
		CHECK(Loom::VisualStudio::IsHeader("Scripts/Tank.HPP"));
		CHECK_FALSE(Loom::VisualStudio::IsHeader("Scripts/Tank.h"));
		CHECK_FALSE(Loom::VisualStudio::IsHeader("Scripts/Tank.hpp.bak"));
	};

	TEST_CASE("a header opens with the solution in the nearest folder above it")
	{
		const Project project("loom visual studio nearest");

		project.Write("Outer.sln");
		const std::filesystem::path solution = project.Write("Scripts/GameScripts.sln");
		const std::filesystem::path header = project.Write("Scripts/Units/Tank.hpp");

		CHECK(std::filesystem::path(Loom::VisualStudio::SolutionFor(header)) == solution);
	};

	TEST_CASE("of several solutions in one folder the first by name wins")
	{
		const Project project("loom visual studio several");

		const std::filesystem::path first = project.Write("Scripts/A.sln");
		project.Write("Scripts/B.sln");
		const std::filesystem::path header = project.Write("Scripts/Tank.hpp");

		CHECK(std::filesystem::path(Loom::VisualStudio::SolutionFor(header)) == first);
	};

	TEST_CASE("a header with no solution above it has none")
	{
		// Holds while the drive root has no solution of its own.
		const std::filesystem::path header = std::filesystem::path(std::filesystem::temp_directory_path().root_path()) / "loom no such folder" / "Tank.hpp";

		CHECK(Loom::VisualStudio::SolutionFor(header).empty());
	};

	TEST_CASE("a path under a stale root is re-rooted to the moved project")
	{
		const Project project("loom visual studio moved");

		const std::filesystem::path moved = project.Write("Scripts/Grid.hpp");
		const std::filesystem::path stale = std::filesystem::temp_directory_path() / "loom visual studio old" / "RTS" / "Scripts" / "Grid.hpp";

		CHECK(Loom::VisualStudio::Reroot(stale, project.root) == Loom::VisualStudio::Normalise(moved));
	};

	TEST_CASE("a path that exists is left where it is")
	{
		const Project project("loom visual studio existing");
		const Project other("loom visual studio other");

		const std::filesystem::path existing = project.Write("Scripts/Grid.hpp");
		other.Write("Scripts/Grid.hpp");

		CHECK(Loom::VisualStudio::Reroot(existing, other.root) == Loom::VisualStudio::Normalise(existing));
	};

	TEST_CASE("a relative path is made absolute and normalised")
	{
		const std::filesystem::path relative = "Scripts/../Scripts/Grid.hpp";
		const std::filesystem::path result = Loom::VisualStudio::Normalise(relative);

		CHECK(result.is_absolute());
		CHECK(result == std::filesystem::absolute("Scripts/Grid.hpp").lexically_normal());
		CHECK(Loom::VisualStudio::Reroot("Scripts/Grid.hpp", std::filesystem::path()) == std::filesystem::absolute("Scripts/Grid.hpp").lexically_normal());
	};

	TEST_CASE("a path found nowhere comes back normalised")
	{
		const Project project("loom visual studio nowhere");

		const std::filesystem::path stale = std::filesystem::temp_directory_path() / "loom visual studio old" / "RTS" / ".." / "Scripts" / "Grid.hpp";

		CHECK(Loom::VisualStudio::Reroot(stale, project.root) == Loom::VisualStudio::Normalise(stale));
	};

	TEST_CASE("an empty root returns the path normalised")
	{
		const std::filesystem::path stale = std::filesystem::temp_directory_path() / "loom visual studio old" / "RTS" / ".." / "Scripts" / "Grid.hpp";

		CHECK(Loom::VisualStudio::Reroot(stale, std::filesystem::path()) == Loom::VisualStudio::Normalise(stale));
	};

	TEST_CASE("the longest tail wins when a shorter one also exists")
	{
		const Project project("loom visual studio longest");

		project.Write("Grid.hpp");
		const std::filesystem::path longest = project.Write("Scripts/Grid.hpp");
		const std::filesystem::path stale = std::filesystem::temp_directory_path() / "loom visual studio old" / "RTS" / "Scripts" / "Grid.hpp";

		CHECK(Loom::VisualStudio::Reroot(stale, project.root) == Loom::VisualStudio::Normalise(longest));
	};
};

#endif
