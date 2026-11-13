#include "doctest.h"

#include "ExtensionLibrary.h"
#include "ExtensionRegistry.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <list>
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
	const void* starting_module = nullptr;

	const void* EditorModule()
	{
#ifdef _WIN32
		return GetModuleHandleW(nullptr);
#else
		void* const module = dlopen(nullptr, RTLD_NOW);
		dlclose(module);

		return module;
#endif
	};

	void AddOwnedPanel(const char* name)
	{
		Loom::ExtensionRegistry::AddPanel("A", name, [](const std::string&) { });
		Loom::ExtensionRegistry::Panels().back().owner = starting_module;
	};

	void AddOwnedItem(const char* name)
	{
		Loom::ExtensionRegistry::AddMenuItem("File", name, [](const std::string&) { });
		Loom::ExtensionRegistry::MenuItems().back().owner = starting_module;
	};

	int RightAbi() { return Loom::ExtensionAbi; };
	int WrongAbi() { return Loom::ExtensionAbi + 1; };
	void StartQuietly() { };
	void CountStop() { stopped++; };
	void ThrowOnStop() { throw std::runtime_error("no"); };

	void AddPanelAndItem()
	{
		AddOwnedPanel("Owned Panel");
		AddOwnedItem("Owned Item");
	};

	void AddItemAndThrow()
	{
		AddOwnedPanel("Half Panel");
		AddOwnedItem("Half Item");

		throw std::runtime_error("no");
	};

	void AddOtherPanelAndItem()
	{
		AddOwnedPanel("Other Panel");
		AddOwnedItem("Other Item");
	};

#ifndef __EMSCRIPTEN__
	void* OpenSystemLibrary(const bool first)
	{
#ifdef _WIN32
		return LoadLibraryA(first ? "kernel32.dll" : "user32.dll");
#else
		return dlopen(first ? "libm.so.6" : "libdl.so.2", RTLD_NOW);
#endif
	};
#endif

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
		AddOwnedPanel("Half Started");

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

		const std::list<Loom::ExtensionRegistry::Panel>& panels = Loom::ExtensionRegistry::Panels();

		CHECK_FALSE(std::prev(panels.end(), 2)->update);

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

		int module = 0;
		starting_module = &module;

		Loom::ExtensionLibrary library;

		CHECK(library.Admit({ RightAbi, AddPanelAndThrow, CountStop }, &module, { }) == "no");
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

		const std::list<Loom::ExtensionRegistry::MenuItem>& items = Loom::ExtensionRegistry::MenuItems();
		const Loom::ExtensionRegistry::MenuItem& plain = items.front();
		const Loom::ExtensionRegistry::MenuItem& full = items.back();

		REQUIRE(items.size() == 2);

		CHECK(plain.menu == "File");
		CHECK(plain.name == "Plain");
		CHECK_FALSE(plain.enabled);
		CHECK_FALSE(plain.tooltip);
		CHECK(plain.owner == EditorModule());

		plain.select("Project");

		CHECK(selected == "Project");

		CHECK(full.menu == "Tools");
		CHECK(full.name == "Full");
		CHECK(full.owner == EditorModule());
		CHECK_FALSE(full.enabled(""));
		CHECK(full.enabled("P"));
		CHECK(full.tooltip("P") == "Tip P");
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

#ifndef __EMSCRIPTEN__
	TEST_CASE("an entry belongs to the module its adding call came from")
	{
		const ClearRegistry clear;

		Loom::ExtensionRegistry::AddPanel("A", "Before", [](const std::string&) { });
		Loom::ExtensionRegistry::AddMenuItem("File", "Before Item", [](const std::string&) { });

		REQUIRE(EditorModule() != nullptr);
		CHECK(Loom::ExtensionRegistry::Panels().back().owner == EditorModule());
		CHECK(Loom::ExtensionRegistry::MenuItems().back().owner == EditorModule());
	};
#endif

	TEST_CASE("a started extension's entries keep the module their calls named")
	{
		const ClearRegistry clear;
		int module = 0;
		starting_module = &module;

		Loom::ExtensionRegistry::AddPanel("A", "Before", [](const std::string&) { });

		Loom::ExtensionLibrary library;

		CHECK_FALSE(library.Admit({ RightAbi, AddPanelAndItem, CountStop }, &module, { }).has_value());

		const std::list<Loom::ExtensionRegistry::Panel>& panels = Loom::ExtensionRegistry::Panels();

		REQUIRE(panels.size() == 2);
		REQUIRE(Loom::ExtensionRegistry::MenuItems().size() == 1);

		CHECK(panels.front().owner == EditorModule());
		CHECK(panels.back().owner == &module);
		CHECK(Loom::ExtensionRegistry::MenuItems().front().owner == &module);

		library.Unload();
	};

#ifndef __EMSCRIPTEN__
	TEST_CASE("a panel added after start leaves with its library, and another library's panel keeps its place")
	{
		const ClearRegistry clear;

		void* const first = OpenSystemLibrary(true);
		void* const second = OpenSystemLibrary(false);

		REQUIRE(first != nullptr);
		REQUIRE(second != nullptr);

		Loom::ExtensionLibrary one;
		Loom::ExtensionLibrary two;

		starting_module = first;
		CHECK_FALSE(one.Admit({ RightAbi, AddPanelAndItem, CountStop }, first, { }).has_value());

		starting_module = second;
		CHECK_FALSE(two.Admit({ RightAbi, AddOtherPanelAndItem, CountStop }, second, { }).has_value());

		AddOwnedPanel("Late Of Second");
		const Loom::ExtensionRegistry::Panel* const lateOfSecond = &Loom::ExtensionRegistry::Panels().back();
		const Loom::ExtensionRegistry::Panel* const otherPanel = &*std::next(Loom::ExtensionRegistry::Panels().begin());

		starting_module = first;
		AddOwnedPanel("Late Of First");
		AddOwnedItem("Late Item Of First");

		REQUIRE(Loom::ExtensionRegistry::Panels().size() == 4);

		one.Unload();

		REQUIRE(Loom::ExtensionRegistry::Panels().size() == 2);
		REQUIRE(Loom::ExtensionRegistry::MenuItems().size() == 1);
		CHECK(Loom::ExtensionRegistry::MenuItems().front().name == "Other Item");
		CHECK(&Loom::ExtensionRegistry::Panels().front() == otherPanel);
		CHECK(&Loom::ExtensionRegistry::Panels().back() == lateOfSecond);
		CHECK(otherPanel->title == "A Other Panel###Other Panel");
		CHECK(lateOfSecond->title == "A Late Of Second###Late Of Second");

		two.Unload();

		CHECK(Loom::ExtensionRegistry::Panels().empty());
	};
#endif

	TEST_CASE("a failed start removes only its own panels and menu items")
	{
		const ClearRegistry clear;

		int module = 0;
		starting_module = &module;

		Loom::ExtensionRegistry::AddPanel("A", "Earlier", [](const std::string&) { });
		Loom::ExtensionRegistry::AddMenuItem("File", "Earlier Item", [](const std::string&) { });

		Loom::ExtensionLibrary library;

		CHECK(library.Admit({ RightAbi, AddItemAndThrow, CountStop }, &module, { }) == "no");

		REQUIRE(Loom::ExtensionRegistry::Panels().size() == 1);
		REQUIRE(Loom::ExtensionRegistry::MenuItems().size() == 1);
		CHECK(Loom::ExtensionRegistry::Panels().back().title == "A Earlier###Earlier");
		CHECK(Loom::ExtensionRegistry::MenuItems().back().name == "Earlier Item");
	};

#ifndef __EMSCRIPTEN__
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

		starting_module = first;
		CHECK_FALSE(one.Admit({ RightAbi, AddPanelAndItem, CountStop }, first, { }).has_value());
		starting_module = second;
		CHECK_FALSE(two.Admit({ RightAbi, AddOtherPanelAndItem, CountStop }, second, { }).has_value());

		REQUIRE(Loom::ExtensionRegistry::Panels().size() == 3);
		REQUIRE(Loom::ExtensionRegistry::MenuItems().size() == 2);

		one.Unload();

		REQUIRE(Loom::ExtensionRegistry::Panels().size() == 2);
		REQUIRE(Loom::ExtensionRegistry::MenuItems().size() == 1);
		CHECK(Loom::ExtensionRegistry::Panels().front().title == "A Unowned###Unowned");
		CHECK(Loom::ExtensionRegistry::Panels().back().title == "A Other Panel###Other Panel");
		CHECK(Loom::ExtensionRegistry::MenuItems().front().name == "Other Item");

		two.Unload();

		CHECK(Loom::ExtensionRegistry::Panels().size() == 1);
		CHECK(Loom::ExtensionRegistry::MenuItems().empty());
	};
#endif

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
