#include "doctest.h"

#include "ProjectAssets.h"

#include "Utilities/StateReference.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>


namespace
{
	// A scripts project with nothing in it but the namespace it declares.
	struct ScriptsProject
	{
		std::filesystem::path root;
		std::filesystem::path project;

		explicit ScriptsProject(const std::string& name)
			: root(std::filesystem::temp_directory_path() / name),
			  project(root / "GameScripts.vcxproj")
		{
			std::filesystem::remove_all(root);
			std::filesystem::create_directories(root);

			std::ofstream(project) << "<Project>\n  <RootNamespace>GameScripts</RootNamespace>\n</Project>\n";
		};

		~ScriptsProject()
		{
			std::error_code code;
			std::filesystem::remove_all(root, code);
		};

		std::string Create(
			const std::string& name,
			std::string* error = nullptr,
			Loom::ProjectAssets::ScriptKind kind = Loom::ProjectAssets::ScriptKind::Component) const
		{
			return Loom::ProjectAssets::CreateScript(project.string(), root.string(), name, kind, error);
		};
	};

	std::string ReadAll(const std::string& path)
	{
		std::ifstream in(path);
		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	};
};


TEST_SUITE("ProjectAssets")
{
	TEST_CASE("a script is one header, a component in the project's namespace")
	{
		const ScriptsProject scripts("loom project assets script");

		const std::string written = scripts.Create("Spinner");

		REQUIRE(written == (scripts.root / "Spinner.hpp").string());

		const std::string text = ReadAll(written);

		CHECK(text.find("namespace GameScripts") != std::string::npos);
		CHECK(text.find("struct Spinner : Loom::Component<Spinner>") != std::string::npos);
		CHECK(text.find("{NAME}") == std::string::npos);
		CHECK(text.find("{NAMESPACE}") == std::string::npos);

		// Nothing else to edit: the project picks headers up by its glob.
		CHECK(ReadAll(scripts.project.string()).find("Spinner") == std::string::npos);
	};

	TEST_CASE("a state is one header, registered under its own name")
	{
		const ScriptsProject scripts("loom project assets state");

		const std::string text = ReadAll(scripts.Create("Jumping", nullptr, Loom::ProjectAssets::ScriptKind::State));

		CHECK(text.find("namespace GameScripts") != std::string::npos);
		CHECK(text.find("struct Jumping : Loom::State") != std::string::npos);
		CHECK(text.find("HOZER_REGISTER_STATE(Jumping);") != std::string::npos);
		CHECK(text.find("{NAME}") == std::string::npos);
	};

	TEST_CASE("a state machine comes with states of its own, and starts in one")
	{
		const ScriptsProject scripts("loom project assets state machine");

		const std::string text = ReadAll(scripts.Create("Guard", nullptr, Loom::ProjectAssets::ScriptKind::StateMachine));

		CHECK(text.find("struct Guard : Loom::StateMachineOf<Guard>") != std::string::npos);
		CHECK(text.find("HOZER_REGISTER_STATE(GuardWaiting);") != std::string::npos);
		CHECK(text.find("HOZER_REGISTER_STATE(GuardWalking);") != std::string::npos);
		CHECK(text.find("m_start = Loom::StateReference::Of<GuardWaiting>();") != std::string::npos);
		CHECK(text.find("{NAME}") == std::string::npos);
	};

	TEST_CASE("a script cannot take a registered state's name")
	{
		const ScriptsProject scripts("loom project assets registered state");

		Loom::StateRegistry::Register("ProjectAssetsTestState", nullptr);

		CHECK_FALSE(Loom::ProjectAssets::ScriptNameProblem(scripts.project.string(), "ProjectAssetsTestState").empty());

		Loom::StateRegistry::Unregister("ProjectAssetsTestState");
	};

	TEST_CASE("a script name already taken is refused")
	{
		const ScriptsProject scripts("loom project assets taken");

		REQUIRE_FALSE(scripts.Create("Spinner").empty());

		std::string error;

		CHECK(scripts.Create("Spinner", &error).empty());
		CHECK(error.find("Spinner.hpp") != std::string::npos);
	};

	TEST_CASE("a script has to be a C++ type name")
	{
		const ScriptsProject scripts("loom project assets names");

		CHECK_FALSE(Loom::ProjectAssets::ScriptNameProblem(scripts.project.string(), "2D").empty());
		CHECK_FALSE(Loom::ProjectAssets::ScriptNameProblem(scripts.project.string(), "class").empty());
		CHECK(Loom::ProjectAssets::ScriptNameProblem(scripts.project.string(), "Mover_2").empty());
	};

	TEST_CASE("a script cannot take a registered component's name")
	{
		const ScriptsProject scripts("loom project assets registered");

		CHECK_FALSE(Loom::ProjectAssets::ScriptNameProblem(scripts.project.string(), "Camera").empty());
	};

	TEST_CASE("a script cannot take the name of a component another header declares")
	{
		const ScriptsProject scripts("loom project assets declared");

		std::ofstream(scripts.root / "Effects.hpp")
			<< "struct Glow : Loom::Component<Glow> { };\n"
			<< "struct Shimmer final : Loom::Component<Shimmer> { };\n"
			<< "struct GlowSettings { };\n";

		CHECK_FALSE(Loom::ProjectAssets::ScriptNameProblem(scripts.project.string(), "Glow").empty());
		CHECK_FALSE(Loom::ProjectAssets::ScriptNameProblem(scripts.project.string(), "Shimmer").empty());
		CHECK(Loom::ProjectAssets::ScriptNameProblem(scripts.project.string(), "Glo").empty());
	};

	TEST_CASE("a project without a RootNamespace uses its own name")
	{
		const ScriptsProject scripts("loom project assets namespace");

		std::ofstream(scripts.project) << "<Project>\n</Project>\n";

		CHECK(ReadAll(scripts.Create("Spinner")).find("namespace GameScripts") != std::string::npos);
	};

	TEST_CASE("a script opens in Visual Studio, reusing one already running")
	{
		const std::string devenv = "C:/VS/devenv.exe";
		const std::string scripts = "C:/My Game/Scripts";
		const std::string header = scripts + "/Mover.hpp";

		const Loom::ProjectAssets::OpenCommand command = Loom::ProjectAssets::OpenWith(header, scripts, devenv);

		CHECK(command.file == devenv);
		CHECK(command.parameters == "/Edit \"" + header + '"');
	};

	TEST_CASE("the scripts solution and project open as themselves in Visual Studio")
	{
		const std::string devenv = "C:/VS/devenv.exe";
		const std::string scripts = "C:/My Game/Scripts";

		for (const std::string& name : { "GameScripts.sln", "GameScripts.vcxproj" })
		{
			const std::string path = scripts + '/' + name;
			const Loom::ProjectAssets::OpenCommand command = Loom::ProjectAssets::OpenWith(path, scripts, devenv);

			CHECK(command.file == devenv);
			CHECK(command.parameters == '"' + path + '"');
		};
	};

	TEST_CASE("anything else, or any file without Visual Studio 2026, goes to its association")
	{
		const std::string devenv = "C:/VS/devenv.exe";
		const std::string scripts = "C:/My Game/Scripts";
		const std::string shader = "C:/My Game/Assets/Shader.shader";
		const std::string header = scripts + "/Mover.hpp";

		for (const auto& [path, with] : { std::pair{ shader, devenv }, std::pair{ header, std::string() } })
		{
			const Loom::ProjectAssets::OpenCommand command = Loom::ProjectAssets::OpenWith(path, scripts, with);

			CHECK(command.file == path);
			CHECK(command.parameters.empty());
		};
	};
};
