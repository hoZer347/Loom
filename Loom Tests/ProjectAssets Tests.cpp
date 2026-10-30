#include "doctest.h"

#include "AssetKind.h"
#include "ProjectAssets.h"
#include "ProjectTemplate.h"
#include "Shaders.h"

#include "Utilities/StateReference.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>


#pragma region Helpers

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

#pragma endregion


TEST_SUITE("ProjectAssets")
{
	#pragma region New Scripts

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

	TEST_CASE("a dialogue manager is one header, a self-typed DialogueManager")
	{
		const ScriptsProject scripts("loom project assets dialogue manager");

		const std::string text = ReadAll(scripts.Create("Herald", nullptr, Loom::ProjectAssets::ScriptKind::DialogueManager));

		CHECK(text.find("namespace GameScripts") != std::string::npos);
		CHECK(text.find("struct Herald : Loom::DialogueManager<Herald>") != std::string::npos);
		CHECK(text.find("{NAME}") == std::string::npos);
	};

	TEST_CASE("a tests script is one header holding a LOOM_TEST named after it")
	{
		const ScriptsProject scripts("loom project assets tests");

		const std::string text = ReadAll(scripts.Create("LaserTests", nullptr, Loom::ProjectAssets::ScriptKind::Test));

		CHECK(text.find("namespace GameScripts") != std::string::npos);
		CHECK(text.find("LOOM_TEST(\"LaserTests makes a scene\")") != std::string::npos);
		CHECK(text.find("LOOM_CHECK(") != std::string::npos);
		CHECK(text.find("{NAME}") == std::string::npos);
	};

	#pragma endregion

	#pragma region Bindings

	TEST_CASE("a new binding goes at the end of its type, explained when it is the first")
	{
		const std::string text =
			"namespace Game\n"
			"{\n"
			"\tstruct Narrator : Loom::DialogueManager<Narrator>\n"
			"\t{\n"
			"\t\tvoid Brace() { const char* brace = \"}\"; };\n"
			"\t};\n"
			"};\n";

		int line = 0;
		const std::string first = Loom::ProjectAssets::WithDialogueBinding(text, 3, &line);
		const std::string declared = "\t\tLOOM_DIALOGUE_BINDING(NewBinding)(int i, int j)\n";

		REQUIRE(first.find(declared) != std::string::npos);
		CHECK(first.find("// LOOM_DIALOGUE_BINDING(Name) makes") < first.find(declared));
		CHECK(first.find(declared) > first.find("void Brace()"));
		CHECK(first.substr(first.find(declared) + declared.size()) == "\t\t{\n\t\t};\n\t};\n};\n");
		CHECK(line == 1 + (int)std::count(first.begin(), first.begin() + first.find(declared), '\n'));

		const std::string second = Loom::ProjectAssets::WithDialogueBinding(first, 3);

		CHECK(second.find("LOOM_DIALOGUE_BINDING(NewBinding1)(int i, int j)") != std::string::npos);
		CHECK(second.find("// LOOM_DIALOGUE_BINDING(Name) makes") == second.rfind("// LOOM_DIALOGUE_BINDING(Name) makes"));
	};

	TEST_CASE("a new binding opens a type written on one line, and keeps its line endings")
	{
		int line = 0;
		const std::string text = Loom::ProjectAssets::WithDialogueBinding("struct Narrator : Base<Narrator> { };\r\n", 1, &line);

		CHECK(text.find("struct Narrator : Base<Narrator>\r\n") == 0);
		CHECK(text.find("\tLOOM_DIALOGUE_BINDING(NewBinding)(int i, int j)") != std::string::npos);
		CHECK(text.find("struct Narrator : Base<Narrator>\r\n{\r\n") == 0);
		CHECK(text.ends_with("\t};\r\n};\r\n"));
		CHECK(std::count(text.begin(), text.end(), '\n') == std::count(text.begin(), text.end(), '\r'));
		CHECK(line == 7);
	};

	TEST_CASE("a new binding takes a name no binding of either form has, and only the first is explained")
	{
		const std::string text =
			"struct Narrator : Base<Narrator>\n"
			"{\n"
			"\tLoom::DialogueBinding<\"NewBinding\"> a = [this]() { };\n"
			"\tLOOM_DIALOGUE_BINDING(NewBinding1)() { };\n"
			"};\n";

		const std::string added = Loom::ProjectAssets::WithDialogueBinding(text, 1);

		CHECK(added.find("LOOM_DIALOGUE_BINDING(NewBinding2)(int i, int j)") != std::string::npos);
		CHECK(added.find("// LOOM_DIALOGUE_BINDING(Name) makes") == std::string::npos);
	};

	TEST_CASE("a type whose braces never close gets no binding")
	{
		CHECK(Loom::ProjectAssets::WithDialogueBinding("struct Narrator : Base<Narrator>\n{\n", 1).empty());
		CHECK(Loom::ProjectAssets::WithDialogueBinding("struct Narrator;\n", 1).empty());
	};

	#pragma endregion

	#pragma region Names

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

	#pragma endregion

	#pragma region Declared Types

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

	TEST_CASE("a type's declaration line skips forward declarations, comments and longer names")
	{
		const std::string text =
			"struct Glow;\n"
			"// struct Glow : Loom::Component<Glow> { };\n"
			"struct GlowSettings { };\n"
			"struct Glow : Loom::Component<Glow> { };\n"
			"struct LOOM_API Shimmer final\n"
			"\t: Component<Shimmer, \"Effects\">\n"
			"{ };\n"
			"class Flicker\n"
			"{ };\n"
			"struct Outer::Inner { };\n";

		CHECK(Loom::ProjectAssets::DeclarationLine(text, "Glow") == 4);
		CHECK(Loom::ProjectAssets::DeclarationLine(text, "GlowSettings") == 3);
		CHECK(Loom::ProjectAssets::DeclarationLine(text, "Shimmer") == 5);
		CHECK(Loom::ProjectAssets::DeclarationLine(text, "Flicker") == 8);
		CHECK(Loom::ProjectAssets::DeclarationLine(text, "Outer") == 0);
		CHECK(Loom::ProjectAssets::DeclarationLine(text, "Missing") == 0);
		CHECK(Loom::ProjectAssets::DeclarationLine(text, "Glow.*") == 0);
	};

	TEST_CASE("a type's declaration line skips block comments and the template it shares a name with")
	{
		const std::string text =
			"/**\n"
			"* struct Patrol : Component<Patrol> { };\n"
			"*/\n"
			"template <typename Self>\n"
			"struct Machine : Base<Self>\n"
			"{ };\n"
			"template <typename T> struct Patrol { };\n"
			"/* struct Machine { }; */\n"
			"struct Machine : Machine<Machine>\n"
			"{ };\n"
			"struct Patrol final : Component<Patrol> { };\n";

		CHECK(Loom::ProjectAssets::DeclarationLine(text, "Machine") == 9);
		CHECK(Loom::ProjectAssets::DeclarationLine(text, "Patrol") == 11);
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

	#pragma endregion

	#pragma region Projects

	TEST_CASE("scenes, shaders and dialogues open in the editor, the scripts project's sources in Visual Studio")
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
		CHECK(opener("Assets/Inked.loompixel", scripts) == ProjectAssets::Opener::Shaders);
		CHECK(opener("Assets/Intro.dialogue", scripts) == ProjectAssets::Opener::Dialogues);
		CHECK(opener("Assets/Wave.loomprevertex", scripts) == ProjectAssets::Opener::Shaders);
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

	TEST_CASE("scenes, shaders, dialogues, sprites, palettes, textures and sounds are game files, scripts and the project's other files are not")
	{
		CHECK(Loom::ProjectAssets::IsGameFile("Level.loomscene"));
		CHECK(Loom::ProjectAssets::IsGameFile("Flat.shader"));
		CHECK(Loom::ProjectAssets::IsGameFile("Hero.png"));
		CHECK(Loom::ProjectAssets::IsGameFile("Sky.JPG"));
		CHECK(Loom::ProjectAssets::IsGameFile("Bell.wav"));
		CHECK(Loom::ProjectAssets::IsGameFile("Theme.MP3"));
		CHECK(Loom::ProjectAssets::IsGameFile("Rain.flac"));
		CHECK(Loom::ProjectAssets::IsShaderFile("Flat.Shader"));
		CHECK(Loom::ProjectAssets::IsShaderFile("Inked.LoomPixel"));
		CHECK(Loom::ProjectAssets::IsGameFile("Inked.loompixel"));
		CHECK_FALSE(Loom::ProjectAssets::IsShaderFile("Level.loomscene"));
		CHECK(Loom::ProjectAssets::IsGameFile("Hero.loomsprite"));
		CHECK(Loom::ProjectAssets::IsGameFile("Hero.loompalette"));
		CHECK(Loom::ProjectAssets::IsGameFile("Intro.dialogue"));
		CHECK(Loom::ProjectAssets::IsDialogueFile("Intro.Dialogue"));
		CHECK_FALSE(Loom::ProjectAssets::IsDialogueFile("Flat.shader"));
		CHECK(Loom::ProjectAssets::IsGameFile("Wave.loomprevertex"));
		CHECK(Loom::ProjectAssets::IsPreVertexFile("Wave.LoomPreVertex"));
		CHECK_FALSE(Loom::ProjectAssets::IsPreVertexFile("Flat.shader"));

		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("Spinner.hpp"));
		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("Spinner.CPP"));
		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("GameScripts.sln"));
		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("GameScripts.vcxproj"));
		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("GameScripts.vcxproj.filters"));
		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("Game.loomproject"));
		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("README.txt"));
		CHECK_FALSE(Loom::ProjectAssets::IsGameFile("Spinner"));
	};

	#pragma endregion

	#pragma region Moving Files

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

	TEST_CASE("a file renamed keeps its folder, never replaces anything, and may change only its case")
	{
		const std::filesystem::path root = std::filesystem::temp_directory_path() / "loom project assets rename";

		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root);

		const std::string warm = (root / "Warm.loompalette").string();
		std::ofstream(warm) << "#FF0000FF\n";
		std::ofstream(root / "Cold.loompalette") << "#0000FFFF\n";

		const std::string renamed = Loom::ProjectAssets::Rename(warm, "Hot.loompalette");

		CHECK(renamed == (root / "Hot.loompalette").string());
		CHECK(ReadAll(renamed) == "#FF0000FF\n");
		CHECK_FALSE(std::filesystem::exists(warm));

		std::string error;
		CHECK(Loom::ProjectAssets::Rename(renamed, "Cold.loompalette", &error).empty());
		CHECK_FALSE(error.empty());
		CHECK(ReadAll((root / "Cold.loompalette").string()) == "#0000FFFF\n");

		error.clear();
		CHECK(Loom::ProjectAssets::Rename(renamed, "Bad/Name.loompalette", &error).empty());
		CHECK_FALSE(error.empty());
		CHECK(std::filesystem::exists(renamed));

		const std::string recased = Loom::ProjectAssets::Rename(renamed, "HOT.loompalette");
		std::vector<std::string> names;

		for (const auto& entry : std::filesystem::directory_iterator(root))
			names.push_back(entry.path().filename().string());

		CHECK(recased == (root / "HOT.loompalette").string());
		CHECK(std::find(names.begin(), names.end(), "HOT.loompalette") != names.end());

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

	#pragma endregion

	#pragma region Users

	TEST_CASE("what uses a file, a folder or a script is found by the whole name, outside what goes")
	{
		const std::filesystem::path root = std::filesystem::temp_directory_path() / "loom project assets users";

		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root / "Assets" / "Sub");
		std::filesystem::create_directories(root / "Build");
		std::filesystem::create_directories(root / "Scenes");
		std::filesystem::create_directories(root / "Scripts" / ".vs");

		const auto write = [&root](const char* file, const char* text)
		{
			std::ofstream(root / file, std::ios::binary) << text;
		};

		write("Assets/Hero.png", "pixels");
		write("Assets/Sub/Inner.png", "pixels");
		write("Scenes/Main.loomscene", "0 = \"Assets/Hero.png\"\nComponent Spinner(guid)\n");
		write("Scenes/Other.loomscene", "0 = \"MyAssets/Hero.png\"\n0 = \"Assets/Hero.png.bak\"\nComponent SpinnerTwo(guid)\n");
		write("Scenes/Inner.loomscene", "0 = \"Assets/Sub/Inner.png\"\n");
		write("Scripts/Spinner.hpp", "struct Spinner { };\n");
		write("Scripts/Uses.hpp", "GetComponent<Spinner>();\n");
		write("Scripts/.vs/Index.hpp", "Spinner\n");
		write("Scripts/Notes.txt", "Spinner\n");
		write("Build/Stale.hpp", "Spinner\n");

		const std::string skip = (root / "Build").string();
		const auto users = [&root, &skip](std::vector<std::string> paths, std::vector<std::string> names)
		{
			for (std::string& path : paths)
				path = (root / path).lexically_normal().string();

			return Loom::ProjectAssets::UsersOf(root.string(), paths, names, skip);
		};

		using Users = std::vector<std::string>;

		CHECK(users({ "Assets/Hero.png" }, { }) == Users{ "Scenes/Main.loomscene" });
		CHECK(users({ "Assets/Sub" }, { }) == Users{ "Scenes/Inner.loomscene" });
		CHECK(users({ "Assets/Sub/Inner.png", "Scenes/Inner.loomscene" }, { }).empty());
		CHECK(users({ "Scripts/Spinner.hpp" }, { "Spinner" }) == Users{ "Scenes/Main.loomscene", "Scripts/Uses.hpp" });
		CHECK(users({ }, { "Spinner" }) == Users{ "Scenes/Main.loomscene", "Scripts/Spinner.hpp", "Scripts/Uses.hpp" });
		CHECK(users({ }, { "Nothing" }).empty());

		std::filesystem::remove_all(root);
	};

	#pragma endregion

	#pragma region Shaders

	TEST_CASE("a project draws with a new project's shader while it has one, or else its first")
	{
		const std::filesystem::path root = std::filesystem::temp_directory_path() / "loom project assets shader";
		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root / "Assets" / "Units");

		CHECK(Loom::ProjectAssets::ProjectShader("") == Loom::ProjectTemplate::shader_path);
		CHECK(Loom::ProjectAssets::ProjectShader(root.string()) == Loom::ProjectTemplate::shader_path);

		std::ofstream(root / "Assets" / "Units" / "Tank.shader") << "tank";
		std::ofstream(root / "Assets" / "Terrain.shader") << "terrain";
		std::ofstream(root / "Assets" / "Inked.loompixel") << "inked";

		CHECK(Loom::ProjectAssets::ShadersIn(root.string()) == std::vector<std::string>{ "Assets/Terrain.shader", "Assets/Units/Tank.shader" });
		CHECK(Loom::ProjectAssets::ProjectShader(root.string()) == "Assets/Terrain.shader");

		std::ofstream(root / Loom::ProjectTemplate::shader_path) << "template";

		CHECK(Loom::ProjectAssets::ProjectShader(root.string()) == Loom::ProjectTemplate::shader_path);

		std::filesystem::remove_all(root);
	};

	TEST_CASE("a new shader is shaded and shadowed by the scene's light")
	{
		const Loom::Shader::Stages stages = Loom::Shader::Assemble(Loom::ProjectAssets::defaultShader);

		CHECK(stages.settings.shading);
		CHECK(stages.settings.shadows);
		CHECK_FALSE(stages.settings.transparency);
	};

	TEST_CASE("a new pixel shader is a .loompixel file that shows the scene as it is")
	{
		const std::filesystem::path root = std::filesystem::temp_directory_path() / "loom project assets pixel shader";
		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root);

		const std::string path = Loom::ProjectAssets::CreatePixelShader(root.string(), "Inked");

		REQUIRE(path == (root / "Inked.loompixel").string());

		std::ifstream file(path);
		const std::string text(std::istreambuf_iterator<char>(file), { });
		file.close();

		CHECK(text.find("LoomScenePixel(LoomScreenPixel())") != std::string::npos);
#ifndef __EMSCRIPTEN__
		CHECK(Loom::Shader::CheckPixel(text).empty());
#endif
		CHECK(Loom::ProjectAssets::CreatePixelShader(root.string(), "Inked").empty());

		std::filesystem::remove_all(root);
	};

	TEST_CASE("a new compute shader is a .loomcompute file that compiles")
	{
		const std::filesystem::path root = std::filesystem::temp_directory_path() / "loom project assets compute shader";
		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root);

		const std::string path = Loom::ProjectAssets::CreateComputeShader(root.string(), "Swarm");

		REQUIRE(path == (root / "Swarm.loomcompute").string());

		std::ifstream file(path);
		const std::string text(std::istreambuf_iterator<char>(file), { });
		file.close();

		CHECK(text.find("vec4 Compute(ivec2 cell, int layer)") != std::string::npos);
#ifndef __EMSCRIPTEN__
		CHECK(Loom::Shader::CheckCompute(text).empty());
#endif
		CHECK(Loom::ProjectAssets::CreateComputeShader(root.string(), "Swarm").empty());

		std::filesystem::remove_all(root);
	};

	#pragma endregion

	#pragma region Asset Kinds

	TEST_CASE("a field taking a pre-vertex file offers New for a pre-vertex")
	{
		CHECK(Loom::AssetKinds::Making({ Loom::ProjectAssets::preVertexExtension }) == Loom::AssetKind::PreVertex);
		CHECK(Loom::AssetKinds::Making({ Loom::ProjectAssets::shaderExtension }) == Loom::AssetKind::Shader);
	};

	TEST_CASE("a field taking only files the editor cannot make offers no New")
	{
		CHECK_FALSE(Loom::AssetKinds::Making({ ".wav", ".ogg" }).has_value());
		CHECK_FALSE(Loom::AssetKinds::Making({ }).has_value());
	};

	TEST_CASE("a scene field offers no New, since a new scene takes the Inspector off it")
	{
		CHECK_FALSE(Loom::AssetKinds::Making({ Loom::SceneSerializer::extension }).has_value());
	};

	TEST_CASE("every kind New makes is written with the extension it is offered for")
	{
		for (const auto& [kind, extension] : Loom::AssetKinds::extensions)
		{
			CHECK(std::string(Loom::AssetKinds::ExtensionOf(kind)) == extension);

			if (kind != Loom::AssetKind::Scene)
				CHECK(Loom::AssetKinds::Making({ extension }) == kind);
		};

		CHECK(std::string(Loom::AssetKinds::ExtensionOf(Loom::AssetKind::Folder)).empty());
	};

	#pragma endregion
};
