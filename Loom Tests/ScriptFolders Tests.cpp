#include "doctest.h"

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
};
