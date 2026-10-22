#include "doctest.h"

#include "FileTab.h"
#include "ScriptFolders.h"

#include <filesystem>
#include <string>
#include <vector>


namespace
{
	using Loom::ScriptFolders;

	// The node at the folder's path under root, or null.
	const ScriptFolders::Node* Find(const ScriptFolders::Node& root, const std::string& folder)
	{
		if (root.path == folder)
			return &root;

		for (const ScriptFolders::Node& child : root.children)
			if (const ScriptFolders::Node* found = Find(child, folder))
				return found;

		return nullptr;
	};

	std::vector<std::string> ScriptsIn(const ScriptFolders& folders, const std::vector<std::string>& scripts, const std::string& folder)
	{
		const ScriptFolders::Node root = folders.Build(scripts);
		const ScriptFolders::Node* node = Find(root, folder);

		return node != nullptr ? node->items : std::vector<std::string>{ };
	};
};


TEST_SUITE("ScriptFolders")
{
	TEST_CASE("scripts filed nowhere are at the top, and one header's types can be filed apart")
	{
		ScriptFolders folders;

		folders.Move("Patrol", "Gameplay/AI");

		const std::vector<std::string> scripts{ "Spinner", "Patrol", "Chase" };

		CHECK(ScriptsIn(folders, scripts, "") == std::vector<std::string>{ "Chase", "Spinner" });
		CHECK(ScriptsIn(folders, scripts, "Gameplay/AI") == std::vector<std::string>{ "Patrol" });
		CHECK(Find(folders.Build(scripts), "Gameplay") != nullptr);

		folders.Move("Patrol", "");

		CHECK(folders.FolderOf("Patrol").empty());
		CHECK(Find(folders.Build(scripts), "Gameplay/AI") != nullptr);
	};

	TEST_CASE("items and folders go in order whatever their case")
	{
		CHECK(ScriptFolders::Alphabetical("glow", "Zone"));
		CHECK_FALSE(ScriptFolders::Alphabetical("Zone", "glow"));
		CHECK_FALSE(ScriptFolders::Alphabetical("Glow", "glow"));

		const ScriptFolders::Node root = ScriptFolders{ }.Build({ "Zone", "glow", "Beam" });

		CHECK(root.items == std::vector<std::string>{ "Beam", "glow", "Zone" });
	};

	TEST_CASE("a folder takes a tidy name that nothing beside it has")
	{
		ScriptFolders folders;

		CHECK(folders.AddFolder("", "  Gameplay ") == "Gameplay");
		CHECK(folders.AddFolder("Gameplay", "AI") == "Gameplay/AI");
		CHECK(folders.AddFolder("", "Gameplay").empty());
		CHECK(folders.AddFolder("", "   ").empty());
		CHECK(folders.AddFolder("", "A/B").empty());
	};

	TEST_CASE("renaming or moving a folder takes what is in it along")
	{
		ScriptFolders folders;

		folders.AddFolder("", "UI");
		folders.Move("Patrol", "Gameplay/AI");
		folders.Move("Health", "Gameplay");

		CHECK(folders.RenameFolder("Gameplay", "Game") == "Game");
		CHECK(folders.FolderOf("Patrol") == "Game/AI");
		CHECK(folders.FolderOf("Health") == "Game");
		CHECK(folders.RenameFolder("Game", "UI").empty());

		CHECK(folders.MoveFolder("Game/AI", "UI"));
		CHECK(folders.FolderOf("Patrol") == "UI/AI");
		CHECK_FALSE(folders.MoveFolder("UI", "UI/AI"));
		CHECK_FALSE(folders.MoveFolder("UI", "UI"));

		const ScriptFolders::Node root = folders.Build({ "Patrol", "Health" });

		CHECK(Find(root, "Gameplay") == nullptr);
		CHECK(Find(root, "Game/AI") == nullptr);
		CHECK(Find(root, "UI/AI") != nullptr);
	};

	TEST_CASE("removing a folder leaves what it held in the one above")
	{
		ScriptFolders folders;

		folders.Move("Health", "Gameplay");
		folders.Move("Patrol", "Gameplay/AI");
		folders.AddFolder("", "AI");

		folders.RemoveFolder("Gameplay");

		CHECK(folders.FolderOf("Health").empty());
		CHECK(folders.FolderOf("Patrol") == "AI");

		const ScriptFolders::Node root = folders.Build({ "Patrol", "Health" });

		CHECK(Find(root, "Gameplay") == nullptr);
		REQUIRE(root.children.size() == 1);
		CHECK(root.children[0].path == "AI");
	};

	TEST_CASE("the folders come back from the file they are saved to, empty ones included")
	{
		const std::filesystem::path file = std::filesystem::temp_directory_path() / "loom script folders.loomscripts";

		ScriptFolders saved;

		saved.AddFolder("", "Empty");
		saved.Move("Patrol", "Gameplay/AI");

		REQUIRE(saved.Save(file.string()));

		ScriptFolders loaded;
		loaded.Move("Stale", "Old");
		loaded.Load(file.string());

		CHECK(loaded.FolderOf("Patrol") == "Gameplay/AI");
		CHECK(loaded.FolderOf("Stale").empty());

		const ScriptFolders::Node root = loaded.Build({ "Patrol" });

		CHECK(Find(root, "Empty") != nullptr);
		CHECK(Find(root, "Old") == nullptr);

		std::filesystem::remove(file);

		loaded.Load(file.string());

		CHECK(loaded.Build({ "Patrol" }).children.empty());
	};

	TEST_CASE("a FileTab's files come back filed by their paths, and its file holds none of another tab's")
	{
		const std::filesystem::path two_d = std::filesystem::temp_directory_path() / "loom 2d folders.loom2d";
		const std::filesystem::path scripts = std::filesystem::temp_directory_path() / "loom 2d folders.loomscripts";

		Loom::FileTab saved(&Loom::FileTabs::TwoD);
		saved.folders.Move("Assets/Units/Tank.png", "Units");
		saved.folders.Move("Assets/Units/Tank=Heavy.png", "Units");

		REQUIRE(saved.Save(two_d.string()));

		ScriptFolders other;
		other.Move("Patrol", "AI");

		REQUIRE(other.Save(scripts.string()));

		Loom::FileTab loaded(&Loom::FileTabs::TwoD);
		loaded.Load(two_d.string());

		CHECK(loaded.folders.FolderOf("Assets/Units/Tank.png") == "Units");
		CHECK(loaded.folders.FolderOf("Assets/Units/Tank=Heavy.png") == "Units");
		CHECK(loaded.folders.Build({ "Assets/Units/Tank=Heavy.png" }).children.size() == 1);

		loaded.Load(scripts.string());

		CHECK(loaded.folders.FolderOf("Patrol").empty());
		CHECK(loaded.folders.AddFolder("", "a=b").empty());

		Loom::FileTab three_d(&Loom::FileTabs::ThreeD);
		three_d.Load(two_d.string());

		CHECK(three_d.folders.FolderOf("Assets/Units/Tank.png").empty());

		std::filesystem::remove(two_d);
		std::filesystem::remove(scripts);
	};

	TEST_CASE("a FileTab's files are in the order of their names, wherever they are")
	{
		const Loom::VirtualFolders folders;

		const std::vector<std::string> items = folders.Build({ "Assets/Units/Tank.glb", "Assets/Buildings/Turret.glb", "Assets/Nature/rock.glb", "Assets/Buildings/Host.glb" }).items;

		CHECK(items == std::vector<std::string>{ "Assets/Buildings/Host.glb", "Assets/Nature/rock.glb", "Assets/Units/Tank.glb", "Assets/Buildings/Turret.glb" });
	};
};
