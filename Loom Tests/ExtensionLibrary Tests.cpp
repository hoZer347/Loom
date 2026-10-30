#include "doctest.h"

#include "ExtensionLibrary.h"
#include "ExtensionRegistry.h"

#include <filesystem>
#include <fstream>
#include <string>

#pragma region Helpers

namespace
{
	struct TempFolder final
	{
		TempFolder() :
			path(std::filesystem::temp_directory_path() / "Loom ExtensionLibrary Tests")
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		};

		~TempFolder()
		{
			std::filesystem::remove_all(path);
		};

		std::filesystem::path path;
	};
};

#pragma endregion

TEST_SUITE("ExtensionLibrary")
{
	TEST_CASE("a panel is titled with its icon and keeps its name as its id")
	{
		Loom::ExtensionRegistry::AddPanel("A", "Test Panel", [](const std::string&) { });

		const Loom::ExtensionRegistry::Panel& panel = Loom::ExtensionRegistry::Panels().back();

		CHECK(panel.title == "A Test Panel###Test Panel");
		CHECK_FALSE(panel.shown);

		Loom::ExtensionRegistry::Panels().pop_back();
	};

	TEST_CASE("a missing folder loads nothing")
	{
		Loom::ExtensionLibrary library;
		library.Load(std::filesystem::temp_directory_path() / "Loom Extensions That Are Not There");

		CHECK(library.Count() == 0);
	};

	TEST_CASE("a file that is no library is skipped")
	{
		const TempFolder folder;
		std::ofstream(folder.path / "Broken.dll") << "not a library";
		std::ofstream(folder.path / "Broken.so") << "not a library";
		std::ofstream(folder.path / "Notes.txt") << "not a library";

		Loom::ExtensionLibrary library;
		library.Load(folder.path);

		CHECK(library.Count() == 0);
	};

	TEST_CASE("unloading drops the extensions' panels")
	{
		Loom::ExtensionRegistry::AddPanel("A", "Left Behind", [](const std::string&) { });

		Loom::ExtensionLibrary library;
		library.Unload();

		CHECK(Loom::ExtensionRegistry::Panels().empty());
	};
};
