#include "doctest.h"

#include "ProjectAssets.h"

#include "Utilities/StateReference.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>


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
		CHECK(text.find("#include \"Loom.h\"") != std::string::npos);
		CHECK(text.find("{NAME}") == std::string::npos);
		CHECK(text.find("{NAMESPACE}") == std::string::npos);

		// Nothing else to edit: the project picks headers up by its glob.
		CHECK(ReadAll(scripts.project.string()).find("Spinner") == std::string::npos);
	};

	TEST_CASE("a state is one header, a self-typed State")
	{
		const ScriptsProject scripts("loom project assets state");

		const std::string text = ReadAll(scripts.Create("Jumping", nullptr, Loom::ProjectAssets::ScriptKind::State));

		CHECK(text.find("namespace GameScripts") != std::string::npos);
		CHECK(text.find("struct Jumping : Loom::State<Jumping>") != std::string::npos);
		CHECK(text.find("#include \"Loom.h\"") != std::string::npos);
		CHECK(text.find("{NAME}") == std::string::npos);
	};

	TEST_CASE("a state machine is one header, a self-typed StateMachine")
	{
		const ScriptsProject scripts("loom project assets state machine");

		const std::string text = ReadAll(scripts.Create("Guard", nullptr, Loom::ProjectAssets::ScriptKind::StateMachine));

		CHECK(text.find("namespace GameScripts") != std::string::npos);
		CHECK(text.find("struct Guard : Loom::StateMachine<Guard>") != std::string::npos);
		CHECK(text.find("#include \"Loom.h\"") != std::string::npos);
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

	TEST_CASE("a script's declared types are the ones it defines, in order, commented out or not")
	{
		const std::string text =
			"struct Forward;\n"
			"// struct Glow : Loom::Component<Glow> { };\n"
			"struct Glow : Loom::Component<Glow> { };\n"
			"class Shimmer final : public Loom::Component<Shimmer> { };\n"
			"struct GlowSettings { };\n";

		CHECK(Loom::ProjectAssets::DeclaredTypes(text) == std::vector<std::string>{ "Glow", "Glow", "Shimmer", "GlowSettings" });
	};

	TEST_CASE("a script is a type that names itself to its base, and a state is one made a State")
	{
		const std::string text =
			"struct Glow;\n"
			"struct Glow : Loom::Component<Glow, \"Effects\"> { };\n"
			"class Shimmer final : public Loom::Component<Shimmer> { };\n"
			"struct St_Idle : Loom::State<St_Idle, Camp> { };\n"
			"struct Camp : Loom::StateMachine<Camp> { };\n"
			"struct GlowSettings { };\n"
			"struct Helper : Base<Other> { };\n";

		const std::vector<Loom::ProjectAssets::DeclaredScript> scripts = Loom::ProjectAssets::DeclaredScripts(text);

		REQUIRE(scripts.size() == 4);
		CHECK(scripts[0].name == "Glow");
		CHECK_FALSE(scripts[0].state);
		CHECK(scripts[1].name == "Shimmer");
		CHECK(scripts[2].name == "St_Idle");
		CHECK(scripts[2].state);
		CHECK(scripts[3].name == "Camp");
		CHECK_FALSE(scripts[3].state);
	};

	TEST_CASE("scenes and shaders open in the editor, the scripts project's sources in Visual Studio")
	{
		using Loom::ProjectAssets;

		const std::string project = (std::filesystem::temp_directory_path() / "loom opener").string();
		const std::string scripts = (std::filesystem::path(project) / "Scripts").string();

		const auto opener = [&](const std::string& relative, const std::string& scripts_folder)
		{
			return ProjectAssets::OpenerOf((std::filesystem::path(project) / relative).string(), scripts_folder);
		};

		CHECK(opener("Scenes/Main.loomscene", scripts) == ProjectAssets::Opener::Editor);
		CHECK(opener("Assets/Lit.shader", scripts) == ProjectAssets::Opener::Shaders);
		CHECK(opener("Assets/Lit.shader", "") == ProjectAssets::Opener::Shaders);
		CHECK(opener("Scripts/Spinner.hpp", scripts) == ProjectAssets::Opener::VisualStudio);
		CHECK(opener("Scripts/Spinner.hpp", "") == ProjectAssets::Opener::Shell);
		CHECK(opener("Tools/Bake.cpp", scripts) == ProjectAssets::Opener::Shell);
		CHECK(opener("Assets/Brick.png", scripts) == ProjectAssets::Opener::Sprites);
		CHECK(opener("Assets/Hero.BMP", "") == ProjectAssets::Opener::Sprites);
		CHECK(opener("Assets/Hero.loomsprite", "") == ProjectAssets::Opener::Sprites);
		CHECK(opener("Assets/Hero.loompalette", "") == ProjectAssets::Opener::Palette);
	};

	TEST_CASE("a project without a RootNamespace uses its own name")
	{
		const ScriptsProject scripts("loom project assets namespace");

		std::ofstream(scripts.project) << "<Project>\n</Project>\n";

		CHECK(ReadAll(scripts.Create("Spinner")).find("namespace GameScripts") != std::string::npos);
	};

	TEST_CASE("scenes, shaders, sprites, palettes, textures and sounds are game files, scripts and the project's other files are not")
	{
		CHECK(Loom::ProjectAssets::IsGameFile("Level.loomscene"));
		CHECK(Loom::ProjectAssets::IsGameFile("Flat.shader"));
		CHECK(Loom::ProjectAssets::IsGameFile("Hero.png"));
		CHECK(Loom::ProjectAssets::IsGameFile("Sky.JPG"));
		CHECK(Loom::ProjectAssets::IsGameFile("Bell.wav"));
		CHECK(Loom::ProjectAssets::IsGameFile("Theme.MP3"));
		CHECK(Loom::ProjectAssets::IsGameFile("Rain.flac"));
		CHECK(Loom::ProjectAssets::IsShaderFile("Flat.Shader"));
		CHECK_FALSE(Loom::ProjectAssets::IsShaderFile("Level.loomscene"));
		CHECK(Loom::ProjectAssets::IsGameFile("Hero.loomsprite"));
		CHECK(Loom::ProjectAssets::IsGameFile("Hero.loompalette"));

		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("Spinner.hpp"));
		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("Spinner.CPP"));
		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("GameScripts.sln"));
		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("GameScripts.vcxproj"));
		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("GameScripts.vcxproj.filters"));
		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("Game.loomproject"));
		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("README.txt"));
		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("Spinner"));
	};

	TEST_CASE("a file copied into a folder keeps its name, or takes the first free number")
	{
		const std::filesystem::path root = std::filesystem::temp_directory_path() / "loom project assets copy";
		const std::filesystem::path outside = root / "Downloads";
		const std::filesystem::path assets = root / "Assets";

		std::filesystem::remove_all(root);
		std::filesystem::create_directories(outside);
		std::filesystem::create_directories(assets);

		const std::string file = (outside / "Hero.png").string();
		std::ofstream(file, std::ios::binary) << "pixels";

		const std::string first = Loom::ProjectAssets::CopyInto(file, assets.string());
		const std::string second = Loom::ProjectAssets::CopyInto(file, assets.string());

		CHECK(first == (assets / "Hero.png").string());
		CHECK(second == (assets / "Hero1.png").string());
		CHECK(ReadAll(second) == "pixels");

		// Dropped back where it already is, nothing is copied.
		CHECK(Loom::ProjectAssets::CopyInto(first, assets.string()) == first);
		CHECK_FALSE(std::filesystem::exists(assets / "Hero2.png"));

		std::string error;
		CHECK(Loom::ProjectAssets::CopyInto((outside / "Missing.png").string(), assets.string(), &error).empty());
		CHECK_FALSE(error.empty());

		CHECK(Loom::ProjectAssets::CopyInto(outside.string(), assets.string(), &error).empty());

		std::filesystem::remove_all(root);
	};

	TEST_CASE("a file or folder moved into a folder keeps its name and never replaces anything")
	{
		const std::filesystem::path root = std::filesystem::temp_directory_path() / "loom project assets move";
		const std::filesystem::path sprites = root / "Sprites";
		const std::filesystem::path art = root / "Art";

		std::filesystem::remove_all(root);
		std::filesystem::create_directories(sprites);
		std::filesystem::create_directories(art);

		const std::string hero = (root / "Hero.png").string();
		std::ofstream(hero, std::ios::binary) << "pixels";

		const std::string moved = Loom::ProjectAssets::MoveInto(hero, sprites.string());

		CHECK(moved == (sprites / "Hero.png").string());
		CHECK(ReadAll(moved) == "pixels");
		CHECK_FALSE(std::filesystem::exists(hero));

		// Dropped back where it already is, nothing happens.
		CHECK(Loom::ProjectAssets::MoveInto(moved, sprites.string()) == moved);

		const std::string folder = Loom::ProjectAssets::MoveInto(sprites.string(), art.string());

		CHECK(folder == (art / "Sprites").string());
		CHECK(std::filesystem::exists(art / "Sprites" / "Hero.png"));

		std::string error;
		CHECK(Loom::ProjectAssets::MoveInto(art.string(), folder, &error).empty());
		CHECK_FALSE(error.empty());
		CHECK(std::filesystem::exists(art));

		std::ofstream(root / "Hero.png", std::ios::binary) << "other";
		error.clear();
		CHECK(Loom::ProjectAssets::MoveInto((root / "Hero.png").string(), folder, &error).empty());
		CHECK_FALSE(error.empty());
		CHECK(ReadAll((art / "Sprites" / "Hero.png").string()) == "pixels");

		CHECK(Loom::ProjectAssets::MoveInto((root / "Missing.png").string(), art.string(), &error).empty());

		std::filesystem::remove_all(root);
	};

	TEST_CASE("a file is named from the project by its own path there, so two of one name stay apart")
	{
		const std::filesystem::path root = std::filesystem::temp_directory_path() / "loom project assets relative";

		CHECK(Loom::ProjectAssets::RelativeTo((root / "Glow.shader").string(), root.string()) == "Glow.shader");
		CHECK(Loom::ProjectAssets::RelativeTo((root / "Assets" / "Glow.shader").string(), root.string()) == "Assets/Glow.shader");
		CHECK(Loom::ProjectAssets::RelativeTo((root / "Assets" / "Old" / "Glow.shader").string(), root.string()) == "Assets/Old/Glow.shader");
		CHECK(Loom::ProjectAssets::RelativeTo((root / "Assets" / "." / "Glow.shader").string(), root.string()) == "Assets/Glow.shader");
	};

	TEST_CASE("a path inside what was moved follows it, and any other stays")
	{
		const std::filesystem::path root = std::filesystem::temp_directory_path() / "loom project assets relocate";
		const std::string from = (root / "Sprites").string();
		const std::string to = (root / "Art" / "Sprites").string();

		CHECK(Loom::ProjectAssets::Relocate(from, from, to) == to);
		CHECK(Loom::ProjectAssets::Relocate((root / "Sprites" / "Hero.png").string(), from, to) == (root / "Art" / "Sprites" / "Hero.png").string());
		CHECK(Loom::ProjectAssets::Relocate((root / "SpritesOld" / "Hero.png").string(), from, to) == (root / "SpritesOld" / "Hero.png").string());
		CHECK(Loom::ProjectAssets::Relocate((root / "Scenes" / "Main.loomscene").string(), from, to) == (root / "Scenes" / "Main.loomscene").string());
	};
};
