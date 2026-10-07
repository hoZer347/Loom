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

	// A folder with a project file in it and nothing else, gone afterwards.
	struct ProjectFolder
	{
		std::filesystem::path root;
		std::filesystem::path file;

		ProjectFolder(const std::string& folder, const std::string& text)
			: root(std::filesystem::temp_directory_path() / folder),
			  file(root / "Game.loomproject")
		{
			std::filesystem::remove_all(root);
			std::filesystem::create_directories(root);

			std::ofstream(file) << text;
		};

		~ProjectFolder()
		{
			std::error_code code;
			std::filesystem::remove_all(root, code);
		};

		void Add(const std::filesystem::path& relative, const std::string& text) const
		{
			std::filesystem::create_directories((root / relative).parent_path());
			std::ofstream(root / relative) << text;
		};

		std::string Ensure(std::string* error = nullptr) const
		{
			return Loom::ProjectTemplate::EnsureScripts(file.string(), error);
		};

		std::string Text() const
		{
			std::ifstream in(file);
			return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		};
	};

	const char* const script_module = "<Project><PreprocessorDefinitions>LOOM_SCRIPT_MODULE</PreprocessorDefinitions>"
		"<TargetName>Found</TargetName></Project>";
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

	TEST_CASE("a project file that names its scripts project is left alone")
	{
		const ProjectFolder project("loom template named", "name=Game\nscripts=Code/Game.vcxproj\nlibrary=Out/Game.dll\n");
		project.Add("Code/Game.vcxproj", script_module);

		const std::string before = project.Text();

		CHECK(project.Ensure() == (project.root / "Code" / "Game.vcxproj").lexically_normal().string());
		CHECK(project.Text() == before);
	};

	TEST_CASE("a scripts project anywhere under the folder is found and written in")
	{
		const ProjectFolder project("loom template found", "name=Game\n");
		project.Add("Source/Deep/Found.vcxproj", script_module);
		project.Add("Tools/Other.vcxproj", "<Project></Project>");

		CHECK(project.Ensure() == (project.root / "Source" / "Deep" / "Found.vcxproj").lexically_normal().string());

		const std::string text = project.Text();

		CHECK(text.find("name=Game\n") != std::string::npos);
		CHECK(text.find("scripts=Source/Deep/Found.vcxproj\n") != std::string::npos);
		CHECK(text.find("library=Build/Found.dll\n") != std::string::npos);
	};

	TEST_CASE("a scripts project that is named but gone is looked for again")
	{
		const ProjectFolder project("loom template gone", "name=Game\nscripts=Old/Gone.vcxproj\nlibrary=Old/Gone.dll\n");
		project.Add("Scripts/Found.vcxproj", script_module);

		CHECK(project.Ensure() == (project.root / "Scripts" / "Found.vcxproj").lexically_normal().string());

		const std::string text = project.Text();

		CHECK(text.find("Gone") == std::string::npos);
		CHECK(text.find("scripts=Scripts/Found.vcxproj\n") != std::string::npos);
	};

	TEST_CASE("a project with no scripts project anywhere is given one")
	{
		const ProjectFolder project("loom template created", "name=Game\n");

		const std::filesystem::path scripts = project.root / "Scripts" / "GameScripts.vcxproj";

		CHECK(project.Ensure() == scripts.lexically_normal().string());

		CHECK(std::filesystem::is_regular_file(scripts));
		CHECK(std::filesystem::is_regular_file(project.root / "Scripts" / "GameScripts.sln"));
		CHECK(std::filesystem::is_regular_file(project.root / "Scripts" / "SpinningTriangle.hpp"));

		const std::string text = project.Text();

		CHECK(text.find("scripts=Scripts/GameScripts.vcxproj\n") != std::string::npos);
		CHECK(text.find("library=Build/GameScripts.dll\n") != std::string::npos);

		// What it wrote is what the next open finds, rather than a second one.
		CHECK(project.Ensure() == scripts.lexically_normal().string());
	};

	TEST_CASE("a project file with no name names its scripts after itself")
	{
		const ProjectFolder project("loom template unnamed", "");

		CHECK(project.Ensure() == (project.root / "Scripts" / "GameScripts.vcxproj").lexically_normal().string());
	};

	TEST_CASE("another project where the new one would go is not written over")
	{
		const ProjectFolder project("loom template in the way", "name=Game\n");
		project.Add("Scripts/GameScripts.vcxproj", "<Project></Project>");

		std::string error;

		CHECK(project.Ensure(&error).empty());
		CHECK(error.find("GameScripts.vcxproj") != std::string::npos);
		CHECK(project.Text() == "name=Game\n");
	};
};

#endif
