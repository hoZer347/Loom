#include "doctest.h"

// The editor only runs on Windows, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "ProjectTemplate.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>


#pragma region Helpers

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

	// The parts of a scripts project the template wrote before it precompiled
	// the engine's header, with the engine named relative to the project.
	const char* const unprecompiled =
		"<Project>\r\n"
		"  <ItemDefinitionGroup>\r\n"
		"    <ClCompile>\r\n"
		"      <PreprocessorDefinitions>LOOM_SCRIPT_MODULE;%(PreprocessorDefinitions)</PreprocessorDefinitions>\r\n"
		"      <AdditionalIncludeDirectories>..\\Engine\\Loom WASM;..\\Engine\\Loom ImGui</AdditionalIncludeDirectories>\r\n"
		"    </ClCompile>\r\n"
		"  </ItemDefinitionGroup>\r\n"
		"  <ItemGroup>\r\n"
		"    <ClCompile Include=\"**\\*.hpp\">\r\n"
		"      <CompileAs>CompileAsCpp</CompileAs>\r\n"
		"    </ClCompile>\r\n"
		"  </ItemGroup>\r\n"
		"</Project>\r\n";

	std::string ReadBytes(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	};

	size_t Count(const std::string& text, const std::string& part)
	{
		size_t count = 0;

		for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size()))
			count++;

		return count;
	};
};

#pragma endregion


TEST_SUITE("ProjectTemplate")
{
	#pragma region Project Files

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

		const std::string vcxproj = ReadAll(scripts);

		CHECK(vcxproj.find("<PrecompiledHeader>Use</PrecompiledHeader>") != std::string::npos);
		CHECK(vcxproj.find("<ForcedIncludeFiles>Loom.h;") != std::string::npos);
		CHECK(vcxproj.find("<ClCompile Include=\"" + Loom::ProjectTemplate::EngineRoot() + "\\Loom WASM\\Loom.h\">") != std::string::npos);
		CHECK(vcxproj.find("<PrecompiledHeader>Create</PrecompiledHeader>") != std::string::npos);
		CHECK(vcxproj.find("<ObjectFileName>$(IntDir)Loom.h.obj</ObjectFileName>") != std::string::npos);
		CHECK(vcxproj.find("{PCH_") == std::string::npos);
		CHECK(vcxproj.find("{HEADERS}") == std::string::npos);
		CHECK(vcxproj.find("<ProjectConfiguration Include=\"Final|x64\">") != std::string::npos);
		CHECK(vcxproj.find("<Import Project=\"" + Loom::ProjectTemplate::EngineRoot() + "\\Property Files\\Final.props\"") != std::string::npos);
		CHECK(ReadAll(project.path / "Scripts" / "MyGameScripts.sln").find("Final|x64 = Final|x64") != std::string::npos);
		CHECK(ReadAll(project.path / "Scripts" / "SpinningTriangle.hpp").find("#include \"Loom.h\"") != std::string::npos);

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

	#pragma endregion

	#pragma region Scripts Projects

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

	TEST_CASE("a found scripts project's library is where its OutDir builds it")
	{
		const ProjectFolder project("loom template out dir", "name=Game\n");
		project.Add("Source/Found.vcxproj",
			"<Project><PreprocessorDefinitions>LOOM_SCRIPT_MODULE</PreprocessorDefinitions>"
			"<OutDir>..\\Bin\\</OutDir><TargetName>Found</TargetName></Project>");

		REQUIRE_FALSE(project.Ensure().empty());

		CHECK(project.Text().find("library=Bin/Found.dll\n") != std::string::npos);
	};

	TEST_CASE("an OutDir made of macros leaves the library in the build folder")
	{
		const ProjectFolder project("loom template out dir macro", "name=Game\n");
		project.Add("Source/Found.vcxproj",
			"<Project><PreprocessorDefinitions>LOOM_SCRIPT_MODULE</PreprocessorDefinitions>"
			"<OutDir>$(SolutionDir)Out\\</OutDir><TargetName>Found</TargetName></Project>");

		REQUIRE_FALSE(project.Ensure().empty());

		CHECK(project.Text().find("library=Build/Found.dll\n") != std::string::npos);
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

	TEST_CASE("a scripts project from before the engine's header was precompiled is given it, once")
	{
		const Folder project("loom template precompile");
		const std::filesystem::path scripts = project.path / "Game.vcxproj";

		std::ofstream(scripts, std::ios::binary) << unprecompiled;

		REQUIRE(Loom::ProjectTemplate::Precompile(scripts.string()));
		REQUIRE(Loom::ProjectTemplate::Precompile(scripts.string()));

		const std::string text = ReadBytes(scripts);

		CHECK(Count(text, "      <PrecompiledHeader>Use</PrecompiledHeader>\r\n") == 1);
		CHECK(Count(text, "<ForcedIncludeFiles>Loom.h;") == 1);
		CHECK(Count(text, "    <ClCompile Include=\"..\\Engine\\Loom WASM\\Loom.h\">\r\n") == 1);
		CHECK(text.find("Loom WASM\\Loom.h\"") < text.find("<ClCompile Include=\"**"));
		CHECK(text.find("<PrecompiledHeader>Use") < text.find("<PreprocessorDefinitions>LOOM_SCRIPT_MODULE"));
		CHECK(Count(text, "\n") == Count(text, "\r\n"));
	};

	TEST_CASE("a scripts project from before Tests ran its cases is given async exceptions, once")
	{
		const Folder project("loom template catch crashes");
		const std::filesystem::path scripts = project.path / "Game.vcxproj";

		std::ofstream(scripts, std::ios::binary) << unprecompiled;

		REQUIRE(Loom::ProjectTemplate::CatchCrashes(scripts.string()));
		REQUIRE(Loom::ProjectTemplate::CatchCrashes(scripts.string()));

		const std::string text = ReadBytes(scripts);

		CHECK(Count(text, "      <ExceptionHandling>Async</ExceptionHandling>\r\n") == 1);
		CHECK(text.find("<ExceptionHandling>") < text.find("<PreprocessorDefinitions>LOOM_SCRIPT_MODULE"));
		CHECK(Count(text, "\n") == Count(text, "\r\n"));
	};

	TEST_CASE("a scripts project with exception handling of its own keeps it")
	{
		const Folder project("loom template own exceptions");
		const std::filesystem::path scripts = project.path / "Game.vcxproj";
		const std::string own = "<Project><ExceptionHandling>Sync</ExceptionHandling><PreprocessorDefinitions>LOOM_SCRIPT_MODULE</PreprocessorDefinitions></Project>";

		std::ofstream(scripts, std::ios::binary) << own;

		REQUIRE(Loom::ProjectTemplate::CatchCrashes(scripts.string()));

		CHECK(ReadBytes(scripts) == own);
	};

	TEST_CASE("a scripts project changed past recognising is not precompiled")
	{
		const Folder project("loom template precompile unknown");
		const std::filesystem::path scripts = project.path / "Game.vcxproj";

		std::ofstream(scripts, std::ios::binary) << script_module;

		REQUIRE(Loom::ProjectTemplate::Precompile(scripts.string()));

		CHECK(ReadBytes(scripts) == script_module);
	};

	TEST_CASE("a project with no scripts project anywhere is given one")
	{
		const ProjectFolder project("loom template created", "name=Game\n");

		const std::filesystem::path scripts = project.root / "Scripts" / "GameScripts.vcxproj";

		CHECK(project.Ensure() == scripts.lexically_normal().string());

		CHECK(std::filesystem::is_regular_file(scripts));
		CHECK(ReadBytes(scripts).find("<ExceptionHandling>Async</ExceptionHandling>") != std::string::npos);
		CHECK(std::filesystem::is_regular_file(project.root / "Scripts" / "GameScripts.sln"));
		CHECK(std::filesystem::is_regular_file(project.root / "Scripts" / "SpinningTriangle.hpp"));

		const std::string text = project.Text();

		CHECK(text.find("scripts=Scripts/GameScripts.vcxproj\n") != std::string::npos);
		CHECK(text.find("library=Build/GameScripts.dll\n") != std::string::npos);

		// What it wrote is what the next open finds, rather than a second one.
		CHECK(project.Ensure() == scripts.lexically_normal().string());
	};

	TEST_CASE("giving a project its scripts and precompiling them prints nothing")
	{
		const ProjectFolder project("loom template quiet", "name=Game\n");

		std::ostringstream captured;
		std::streambuf* const previous = std::cout.rdbuf(captured.rdbuf());

		const std::string scripts = project.Ensure();
		const bool precompiled = Loom::ProjectTemplate::Precompile(scripts);

		std::cout.rdbuf(previous);

		CHECK(!scripts.empty());
		CHECK(precompiled);
		CHECK(captured.str().empty());
	};

	#pragma endregion

	#pragma region Names and Engines

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

	TEST_CASE("an engine is a worktree only when its .git is a file")
	{
		const Folder main("loom template main checkout");
		const Folder worktree("loom template worktree");
		const Folder plain("loom template no git");

		std::filesystem::create_directory(main.path / ".git");
		std::ofstream(worktree.path / ".git") << "gitdir: C:/Loom/.git/worktrees/Loom-1\n";

		CHECK_FALSE(Loom::ProjectTemplate::IsWorktree(main.path.string()));
		CHECK(Loom::ProjectTemplate::IsWorktree(worktree.path.string()));
		CHECK_FALSE(Loom::ProjectTemplate::IsWorktree(plain.path.string()));
	};

	TEST_CASE("a script build's line is shown without its indent and the project it came from")
	{
		const std::string error = "C:\\Games\\Raycasts\\Scripts\\Laser.hpp(111,1): error C2059: syntax error: 'this'";

		CHECK(Loom::ProjectTemplate::BuildLine("  " + error + " [C:\\Games\\Raycasts\\Scripts\\RaycastsScripts.vcxproj]") == error);
		CHECK(Loom::ProjectTemplate::BuildLine("\t" + error) == error);
		CHECK(Loom::ProjectTemplate::BuildLine("   ").empty());
	};

	TEST_CASE("a scripts build names its project and configuration, and keeps quiet about building in the temporary folder")
	{
		const std::string msbuild = "C:\\MSBuild.exe";
		const std::string project = "C:\\Games\\Raycasts\\Scripts\\RaycastsScripts.vcxproj";
		const std::string build = Loom::ProjectTemplate::BuildCommand(msbuild, project, "Release", false);
		const std::string rebuild = Loom::ProjectTemplate::BuildCommand(msbuild, project, "Release", true);

		CHECK(build.starts_with("\"" + msbuild + "\" \"" + project + "\""));
		CHECK(build.find("-p:Configuration=Release") != std::string::npos);
		CHECK(build.find("-p:IgnoreWarnIntDirInTempDetected=true") != std::string::npos);
		CHECK(build.find("-t:Rebuild") == std::string::npos);
		CHECK(rebuild.find("-t:Rebuild") != std::string::npos);
	};

	#pragma endregion
};

#endif
