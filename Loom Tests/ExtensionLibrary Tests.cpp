#include "doctest.h"

#include "ExtensionLibrary.h"
#include "ExtensionRegistry.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

	void AddPanelAndItem()
	{
		Loom::ExtensionRegistry::AddPanel("A", "Owned Panel", [](const std::string&) { });
		Loom::ExtensionRegistry::AddMenuItem("File", "Owned Item", [](const std::string&) { });
	};

	void AddItemAndThrow()
	{
		Loom::ExtensionRegistry::AddPanel("A", "Half Panel", [](const std::string&) { });
		Loom::ExtensionRegistry::AddMenuItem("File", "Half Item", [](const std::string&) { });

		throw std::runtime_error("no");
	};

	void AddOtherPanelAndItem()
	{
		Loom::ExtensionRegistry::AddPanel("A", "Other Panel", [](const std::string&) { });
		Loom::ExtensionRegistry::AddMenuItem("File", "Other Item", [](const std::string&) { });
	};

	void* OpenSystemLibrary(const bool first)
	{
#ifdef _WIN32
		return LoadLibraryA(first ? "kernel32.dll" : "user32.dll");
#else
		return dlopen(first ? "libm.so.6" : "libdl.so.2", RTLD_NOW);
#endif
	};

	struct ClearRegistry final
	{
		~ClearRegistry()
		{
			Loom::ExtensionRegistry::Panels().clear();
			Loom::ExtensionRegistry::MenuItems().clear();
		};
	};

	void AddPanelAndThrow()
	{
		Loom::ExtensionRegistry::AddPanel("A", "Half Started", [](const std::string&) { });

		throw std::runtime_error("no");
	};
};

#pragma endregion

TEST_SUITE("ExtensionLibrary")
{
	TEST_CASE("a panel keeps what takes the files dropped on it")
	{
		std::vector<std::string> taken;

		Loom::ExtensionRegistry::AddPanel("A", "Drop Panel", [](const std::string&) { }, { },
			[&taken](const std::vector<std::string>& files) { taken = files; });

		Loom::ExtensionRegistry::Panels().back().drop({ "shot.png" });
		Loom::ExtensionRegistry::Panels().pop_back();

		CHECK(taken == std::vector<std::string>{ "shot.png" });
	};

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

	TEST_CASE("unloading keeps panels no extension of the library added")
	{
		Loom::ExtensionRegistry::AddPanel("A", "Left Behind", [](const std::string&) { });

		Loom::ExtensionLibrary library;
		library.Unload();

		CHECK(Loom::ExtensionRegistry::Panels().size() == 1);

		Loom::ExtensionRegistry::Panels().clear();
	};

	TEST_CASE("a menu item keeps its menu, name and callbacks, and none it leaves out")
	{
		const ClearRegistry clear;
		std::string selected;

		Loom::ExtensionRegistry::AddMenuItem("File", "Plain", [&selected](const std::string& project) { selected = project; });
		Loom::ExtensionRegistry::AddMenuItem(
			"Tools",
			"Full",
			[](const std::string&) { },
			[](const std::string& project) { return !project.empty(); },
			[](const std::string& project) { return "Tip " + project; });

		const std::deque<Loom::ExtensionRegistry::MenuItem>& items = Loom::ExtensionRegistry::MenuItems();

		REQUIRE(items.size() == 2);

		CHECK(items[0].menu == "File");
		CHECK(items[0].name == "Plain");
		CHECK_FALSE(items[0].enabled);
		CHECK_FALSE(items[0].tooltip);
		CHECK(items[0].owner == nullptr);

		items[0].select("Project");

		CHECK(selected == "Project");

		CHECK(items[1].menu == "Tools");
		CHECK(items[1].name == "Full");
		CHECK(items[1].owner == nullptr);
		CHECK_FALSE(items[1].enabled(""));
		CHECK(items[1].enabled("P"));
		CHECK(items[1].tooltip("P") == "Tip P");
	};

	TEST_CASE("menu items keep the order they were added in, apart from panels")
	{
		const ClearRegistry clear;

		Loom::ExtensionRegistry::AddMenuItem("File", "First", [](const std::string&) { });
		Loom::ExtensionRegistry::AddMenuItem("Edit", "Second", [](const std::string&) { });

		CHECK(Loom::ExtensionRegistry::Panels().empty());
		CHECK(Loom::ExtensionRegistry::MenuItems().front().name == "First");
		CHECK(Loom::ExtensionRegistry::MenuItems().back().name == "Second");
	};

	TEST_CASE("a started extension's panels and menu items belong to its module")
	{
		const ClearRegistry clear;
		int module = 0;

		Loom::ExtensionRegistry::AddPanel("A", "Before", [](const std::string&) { });

		Loom::ExtensionLibrary library;

		CHECK_FALSE(library.Admit({ RightAbi, AddPanelAndItem, CountStop }, &module, { }).has_value());

		const std::deque<Loom::ExtensionRegistry::Panel>& panels = Loom::ExtensionRegistry::Panels();

		REQUIRE(panels.size() == 2);
		REQUIRE(Loom::ExtensionRegistry::MenuItems().size() == 1);

		CHECK(panels[0].owner == nullptr);
		CHECK(panels[1].owner == &module);
		CHECK(Loom::ExtensionRegistry::MenuItems()[0].owner == &module);

		library.Unload();
	};

	TEST_CASE("a failed start removes only its own panels and menu items")
	{
		const ClearRegistry clear;

		Loom::ExtensionRegistry::AddPanel("A", "Earlier", [](const std::string&) { });
		Loom::ExtensionRegistry::AddMenuItem("File", "Earlier Item", [](const std::string&) { });

		Loom::ExtensionLibrary library;

		CHECK(library.Admit({ RightAbi, AddItemAndThrow, CountStop }, nullptr, { }) == "no");

		REQUIRE(Loom::ExtensionRegistry::Panels().size() == 1);
		REQUIRE(Loom::ExtensionRegistry::MenuItems().size() == 1);
		CHECK(Loom::ExtensionRegistry::Panels().back().title == "A Earlier###Earlier");
		CHECK(Loom::ExtensionRegistry::MenuItems().back().name == "Earlier Item");
	};

	TEST_CASE("unloading one library leaves another library's panels and menu items")
	{
		const ClearRegistry clear;

		void* const first = OpenSystemLibrary(true);
		void* const second = OpenSystemLibrary(false);

		REQUIRE(first != nullptr);
		REQUIRE(second != nullptr);

		Loom::ExtensionRegistry::AddPanel("A", "Unowned", [](const std::string&) { });

		Loom::ExtensionLibrary one;
		Loom::ExtensionLibrary two;

		CHECK_FALSE(one.Admit({ RightAbi, AddPanelAndItem, CountStop }, first, { }).has_value());
		CHECK_FALSE(two.Admit({ RightAbi, AddOtherPanelAndItem, CountStop }, second, { }).has_value());

		REQUIRE(Loom::ExtensionRegistry::Panels().size() == 3);
		REQUIRE(Loom::ExtensionRegistry::MenuItems().size() == 2);

		one.Unload();

		REQUIRE(Loom::ExtensionRegistry::Panels().size() == 2);
		REQUIRE(Loom::ExtensionRegistry::MenuItems().size() == 1);
		CHECK(Loom::ExtensionRegistry::Panels()[0].title == "A Unowned###Unowned");
		CHECK(Loom::ExtensionRegistry::Panels()[1].title == "A Other Panel###Other Panel");
		CHECK(Loom::ExtensionRegistry::MenuItems()[0].name == "Other Item");

		two.Unload();

		CHECK(Loom::ExtensionRegistry::Panels().size() == 1);
		CHECK(Loom::ExtensionRegistry::MenuItems().empty());
	};

	TEST_CASE("opening a file that is not there reports it and loads nothing")
	{
		const TempFolder folder;
		const std::filesystem::path missing = folder.path / "Nothing.dll";

		std::ostringstream captured;
		std::streambuf* const previous = std::cerr.rdbuf(captured.rdbuf());

		Loom::ExtensionLibrary library;
		library.Open(missing);

		std::cerr.rdbuf(previous);

		CHECK(captured.str().find("Could not find the extension " + missing.string()) != std::string::npos);
		CHECK(library.Count() == 0);
	};
};
