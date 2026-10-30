#include "doctest.h"

#include "ExtensionLibrary.h"
#include "ExtensionRegistry.h"

#include <deque>
#include <filesystem>
#include <fstream>
#include <stdexcept>
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

	int stopped = 0;

	int RightAbi() { return Loom::ExtensionAbi; };
	int WrongAbi() { return Loom::ExtensionAbi + 1; };
	void StartQuietly() { };
	void CountStop() { stopped++; };
	void ThrowOnStop() { throw std::runtime_error("no"); };

	void AddPanelAndThrow()
	{
		Loom::ExtensionRegistry::AddPanel("A", "Half Started", [](const std::string&) { });

		throw std::runtime_error("no");
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

	TEST_CASE("a panel may leave update out, and keeps the one it is given")
	{
		int updates = 0;

		Loom::ExtensionRegistry::AddPanel("A", "Drawn", [](const std::string&) { });
		Loom::ExtensionRegistry::AddPanel(
			"A",
			"Updated",
			[](const std::string&) { },
			[&updates](const std::string&) { updates++; });

		const std::deque<Loom::ExtensionRegistry::Panel>& panels = Loom::ExtensionRegistry::Panels();

		CHECK_FALSE(panels[panels.size() - 2].update);

		panels.back().update("Project");

		CHECK(updates == 1);

		Loom::ExtensionRegistry::Panels().clear();
	};

	TEST_CASE("a right ABI is admitted and stopped on unload")
	{
		stopped = 0;

		Loom::ExtensionLibrary library;

		CHECK_FALSE(library.Admit({ RightAbi, StartQuietly, CountStop }, nullptr, { }).has_value());
		CHECK(library.Count() == 1);

		library.Unload();

		CHECK(stopped == 1);
		CHECK(library.Count() == 0);
	};

	TEST_CASE("a wrong ABI is refused without starting")
	{
		Loom::ExtensionLibrary library;

		CHECK(library.Admit({ WrongAbi, AddPanelAndThrow, CountStop }, nullptr, { }).has_value());
		CHECK(library.Count() == 0);
		CHECK(Loom::ExtensionRegistry::Panels().empty());
	};

	TEST_CASE("a missing export is refused")
	{
		Loom::ExtensionLibrary library;

		CHECK(library.Admit({ RightAbi, StartQuietly, nullptr }, nullptr, { }).has_value());
		CHECK(library.Admit({ RightAbi, nullptr, CountStop }, nullptr, { }).has_value());
		CHECK(library.Admit({ nullptr, StartQuietly, CountStop }, nullptr, { }).has_value());
		CHECK(library.Count() == 0);
	};

	TEST_CASE("a throwing start takes back the panels it added and leaves others")
	{
		Loom::ExtensionRegistry::AddPanel("A", "Earlier", [](const std::string&) { });

		Loom::ExtensionLibrary library;

		CHECK(library.Admit({ RightAbi, AddPanelAndThrow, CountStop }, nullptr, { }) == "no");
		CHECK(library.Count() == 0);

		REQUIRE(Loom::ExtensionRegistry::Panels().size() == 1);
		CHECK(Loom::ExtensionRegistry::Panels().back().title == "A Earlier###Earlier");

		Loom::ExtensionRegistry::Panels().clear();
	};

	TEST_CASE("a throwing stop does not keep the others from stopping")
	{
		stopped = 0;

		Loom::ExtensionLibrary library;

		CHECK_FALSE(library.Admit({ RightAbi, StartQuietly, CountStop }, nullptr, { }).has_value());
		CHECK_FALSE(library.Admit({ RightAbi, StartQuietly, ThrowOnStop }, nullptr, { }).has_value());

		library.Unload();

		CHECK(stopped == 1);
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
