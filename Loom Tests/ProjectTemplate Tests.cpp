#include "doctest.h"

// The editor only runs on Windows, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "ProjectTemplate.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>


namespace
{
	// An empty folder under the temp directory, gone again afterwards.
	struct Folder
	{
		std::filesystem::path path;

		explicit Folder(const std::string& name)
			: path(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		};

		~Folder()
		{
			std::error_code code;
			std::filesystem::remove_all(path, code);
		};
	};

	std::string ReadAll(const std::filesystem::path& path)
	{
		std::ifstream in(path);
		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	};
};


TEST_SUITE("ProjectTemplate")
{
	TEST_CASE("a project without a project file gets one pointing at new scripts")
	{
		const Folder project("loom template My Game");

		std::string error;

		const std::string scripts = Loom::ProjectTemplate::AddScripts(project.path.string(), "My Game", &error);

		REQUIRE_MESSAGE(!scripts.empty(), error);

		CHECK(scripts == (project.path / "Scripts" / "MyGameScripts.vcxproj").string());
		CHECK(scripts == Loom::ProjectTemplate::ScriptsProject(project.path.string(), "My Game"));
		CHECK(std::filesystem::exists(project.path / "Scripts" / "MyGameScripts.sln"));
		CHECK(ReadAll(scripts).find("<RootNamespace>MyGameScripts</RootNamespace>") != std::string::npos);

		const std::string file = ReadAll(project.path / "MyGame.loomproject");

		CHECK(file.find("name=MyGame\n") != std::string::npos);
		CHECK(file.find("scripts=Scripts/MyGameScripts.vcxproj\n") != std::string::npos);
		CHECK(file.find("library=Build/MyGameScripts.dll\n") != std::string::npos);
	};

	TEST_CASE("an existing project file keeps what it had and is pointed at the scripts once")
	{
		const Folder project("loom template existing");

		std::ofstream(project.path / "Game.loomproject") << "name=Game";

		REQUIRE_FALSE(Loom::ProjectTemplate::AddScripts(project.path.string(), "Game").empty());
		REQUIRE_FALSE(Loom::ProjectTemplate::AddScripts(project.path.string(), "Game").empty());

		CHECK(ReadAll(project.path / "Game.loomproject") ==
			"name=Game\n"
			"scripts=Scripts/GameScripts.vcxproj\n"
			"library=Build/GameScripts.dll\n");
	};

	TEST_CASE("a name MSBuild cannot take is made into one it can")
	{
		CHECK(Loom::ProjectTemplate::UsableName("My Game!") == "MyGame");
		CHECK(Loom::ProjectTemplate::UsableName("3D") == "Project3D");
		CHECK(Loom::ProjectTemplate::UsableName("   ") == "Project");
		CHECK(Loom::ProjectTemplate::UsableName(std::string(Loom::ProjectTemplate::maxNameLength * 2, 'a')).size() ==
			Loom::ProjectTemplate::maxNameLength);
	};
};

#endif
