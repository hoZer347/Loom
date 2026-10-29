#include "doctest.h"

// Explorer's file types, which Linux and the web test build have none of.
#ifdef _WIN32

#include "InputActions.h"
#include "Prefab.h"
#include "PreVertex.h"
#include "SceneSerializer.h"
#include "Shaders.h"

#include "BuildSettings.h"
#include "FileTab.h"
#include "FileTypes.h"
#include "ProjectTemplate.h"
#include "ScriptFolders.h"
#include "SpriteAsset.h"
#include "VisualStudio.h"

#include <process.h>
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>


namespace
{
	// Under HKEY_CURRENT_USER, apart from the classes Explorer reads and from
	// another suite running at the same time.
	const std::wstring TEST_KEY = L"Software\\Loom Tests " + std::to_wstring(_getpid());
	const std::wstring TEST_CLASSES = TEST_KEY + L"\\Classes";

	// An engine checkout under the temp directory with an editor built in it,
	// gone again afterwards with the classes the test registered.
	struct Engine
	{
		std::filesystem::path root;
		std::filesystem::path editor;

		explicit Engine(const std::string& name)
			: root(std::filesystem::temp_directory_path() / name),
			editor(root / "x64" / "Release" / "Loom Editor.exe")
		{
			std::filesystem::remove_all(root);
			std::filesystem::create_directories(root / "Loom WASM");
			std::filesystem::create_directories(editor.parent_path());
			std::ofstream(root / "Loom WASM" / "Loom.h");
			RegDeleteTreeW(HKEY_CURRENT_USER, TEST_KEY.c_str());
		};

		~Engine()
		{
			std::error_code code;
			std::filesystem::remove_all(root, code);
			RegDeleteTreeW(HKEY_CURRENT_USER, TEST_KEY.c_str());
		};
	};

	std::wstring CommandFor(const std::filesystem::path& editor)
	{
		return L"\"" + editor.wstring() + L"\" \"%1\"";
	};
};


TEST_SUITE("FileTypes")
{
	TEST_CASE("every file the editor opens is one of its file types")
	{
		const char* opened[] =
		{
			Loom::ProjectTemplate::extension,
			Loom::SceneSerializer::extension,
			Loom::Prefab::extension,
			Loom::SpriteAsset::extension,
			Loom::Palette::extension,
			Loom::BuildSettings::extension,
			Loom::ScriptFolders::extension,
			Loom::FileTabs::TwoD.extension,
			Loom::FileTabs::ThreeD.extension,
			Loom::FileTabs::Sounds.extension,
			Loom::FileTabs::Fonts.extension,
			Loom::InputActions::extension,
			Loom::Shader::pixel_extension,
			Loom::Shader::compute_extension,
			Loom::PreVertex::extension,
			Loom::VisualStudio::header_extension,
		};

		const auto types = Loom::FileTypes::All();

		CHECK(types.size() == std::size(opened));

		for (const char* extension : opened)
			CHECK_MESSAGE(std::any_of(types.begin(), types.end(),
				[&](const Loom::FileTypes::Type& type) { return std::string_view(type.extension) == extension; }),
				extension);
	};

	TEST_CASE("the main checkout's editor opens every file type")
	{
		const Engine engine("loom file types main checkout");

		std::filesystem::create_directory(engine.root / ".git");

		CHECK(Loom::FileTypes::MayClaim(engine.editor));
		CHECK(Loom::FileTypes::Claim(engine.editor, TEST_CLASSES));

		for (const Loom::FileTypes::Type& type : Loom::FileTypes::All())
			CHECK_MESSAGE(Loom::FileTypes::OpenCommand(type.extension, TEST_CLASSES) == CommandFor(engine.editor), type.extension);
	};

	TEST_CASE("a worktree's editor leaves the file types to the main checkout's")
	{
		const Engine main("loom file types main checkout");
		const Engine slot("loom file types worktree");

		std::filesystem::create_directory(main.root / ".git");
		std::ofstream(slot.root / ".git") << "gitdir: C:/Loom/.git/worktrees/Loom-1\n";

		REQUIRE(Loom::FileTypes::Claim(main.editor, TEST_CLASSES));

		CHECK_FALSE(Loom::FileTypes::MayClaim(slot.editor));
		CHECK_FALSE(Loom::FileTypes::Claim(slot.editor, TEST_CLASSES));

		for (const Loom::FileTypes::Type& type : Loom::FileTypes::All())
			CHECK_MESSAGE(Loom::FileTypes::OpenCommand(type.extension, TEST_CLASSES) == CommandFor(main.editor), type.extension);
	};

	TEST_CASE("a Debug editor leaves the file types to the Release one")
	{
		const Engine engine("loom file types debug");
		const std::filesystem::path debug = engine.root / "x64" / "Debug" / "Loom Editor.exe";

		std::filesystem::create_directory(engine.root / ".git");

		CHECK_FALSE(Loom::FileTypes::MayClaim(debug));
		CHECK_FALSE(Loom::FileTypes::Claim(debug, TEST_CLASSES));
		CHECK(Loom::FileTypes::OpenCommand(Loom::SceneSerializer::extension, TEST_CLASSES).empty());
	};

	TEST_CASE("an editor outside any engine takes no file types")
	{
		const std::filesystem::path stray = std::filesystem::temp_directory_path() / "loom file types stray" / "Loom Editor.exe";

		CHECK_FALSE(Loom::FileTypes::MayClaim(stray));
		CHECK_FALSE(Loom::FileTypes::Claim(stray, TEST_CLASSES));
		CHECK(Loom::FileTypes::OpenCommand(Loom::SceneSerializer::extension, TEST_CLASSES).empty());
	};
};

#endif
