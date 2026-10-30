#include "doctest.h"

#include "AssetTrash.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>


#pragma region Helpers

namespace
{
	struct Project
	{
		std::filesystem::path root = std::filesystem::temp_directory_path() / "LoomAssetTrashTests";

		Project()
		{
			Clean();
			std::filesystem::create_directories(root);
		};

		~Project() { Clean(); };

		void Clean() const
		{
			std::error_code code;
			std::filesystem::remove_all(root, code);
		};

		std::string Write(const std::string& name, const std::string& text) const
		{
			const std::filesystem::path path = root / name;

			std::filesystem::create_directories(path.parent_path());
			std::ofstream(path, std::ios::binary) << text;

			return path.string();
		};
	};

	std::string Read(const std::string& path)
	{
		std::ifstream file(path, std::ios::binary);

		return std::string(std::istreambuf_iterator<char>(file), { });
	};

	bool There(const std::string& path)
	{
		return std::filesystem::exists(path);
	};

#ifdef _WIN32
	// The same folder through the machine's admin share, which is another
	// volume to Windows, or none where the share is off.
	std::filesystem::path OnAnotherVolume(const std::filesystem::path& folder)
	{
		std::error_code code;
		const std::string text = folder.string();
		const std::filesystem::path share = "\\\\localhost\\" + text.substr(0, 1) + "$" + text.substr(text.find(':') + 1);

		return std::filesystem::exists(share, code) ? share : std::filesystem::path();
	};
#endif
};

#pragma endregion

TEST_SUITE("AssetTrash")
{
	TEST_CASE("a delete takes the file away, undo puts it back and redo takes it again")
	{
		const Project project;
		const std::string file = project.Write("Scene.loomscene", "held");
		Loom::AssetTrash trash([](const std::string&) { });

		REQUIRE(trash.Delete({ file }));
		CHECK_FALSE(There(file));
		CHECK(trash.CanUndo());
		CHECK_FALSE(trash.CanRedo());

		REQUIRE(trash.Step(true));
		CHECK(Read(file) == "held");
		CHECK(trash.CanRedo());

		REQUIRE(trash.Step(false));
		CHECK_FALSE(There(file));

		REQUIRE(trash.Step(true));
		CHECK(Read(file) == "held");
	};

	TEST_CASE("a folder comes back with what was in it")
	{
		const Project project;
		const std::string inside = project.Write("Folder/Deep/Inside.png", "pixels");
		const std::string folder = (project.root / "Folder").string();
		Loom::AssetTrash trash([](const std::string&) { });

		REQUIRE(trash.Delete({ folder }));
		CHECK_FALSE(There(folder));

		REQUIRE(trash.Step(true));
		CHECK(Read(inside) == "pixels");
	};

	TEST_CASE("files deleted together step together, and steps go back newest first")
	{
		const Project project;
		const std::string image = project.Write("Hero.png", "image");
		const std::string sprite = project.Write("Hero.loomsprite", "sprite");
		const std::string other = project.Write("Other.png", "other");
		Loom::AssetTrash trash([](const std::string&) { });

		REQUIRE(trash.Delete({ image, sprite }));
		REQUIRE(trash.Delete({ other }));
		CHECK(trash.Next(true) == std::vector<std::string>{ other });

		REQUIRE(trash.Step(true));
		CHECK(There(other));
		CHECK_FALSE(There(image));

		REQUIRE(trash.Step(true));
		CHECK(There(image));
		CHECK(There(sprite));
		CHECK_FALSE(trash.Step(true));
	};

	TEST_CASE("undo replaces nothing, and the step stays for when the path is free")
	{
		const Project project;
		const std::string file = project.Write("Scene.loomscene", "deleted");
		Loom::AssetTrash trash([](const std::string&) { });

		REQUIRE(trash.Delete({ file }));

		project.Write("Scene.loomscene", "made since");

		std::string error;

		CHECK_FALSE(trash.Step(true, &error));
		CHECK_FALSE(error.empty());
		CHECK(Read(file) == "made since");
		CHECK(trash.CanUndo());

		std::filesystem::remove(file);

		REQUIRE(trash.Step(true));
		CHECK(Read(file) == "deleted");
	};

	TEST_CASE("a path that cannot be taken leaves the others where they were")
	{
		const Project project;
		const std::string file = project.Write("Scene.loomscene", "kept");
		const std::string missing = (project.root / "Missing.loomscene").string();
		Loom::AssetTrash trash([](const std::string&) { });

		std::string error;

		CHECK_FALSE(trash.Delete({ file, missing }, &error));
		CHECK_FALSE(error.empty());
		CHECK(Read(file) == "kept");
		CHECK_FALSE(trash.CanUndo());
	};

	TEST_CASE("emptying discards from where the file was, and from the trash when that is taken")
	{
		const Project project;
		const std::string free = project.Write("Free.png", "free");
		const std::string taken = project.Write("Taken.png", "deleted");
		std::vector<std::string> discarded{ };

		Loom::AssetTrash trash(
			[&discarded](const std::string& path)
			{
				discarded.push_back(path);
				std::filesystem::remove(path);
			});

		REQUIRE(trash.Delete({ free }));
		REQUIRE(trash.Delete({ taken }));

		project.Write("Taken.png", "made since");

		trash.Empty();

		CHECK(discarded.size() == 2);
		CHECK(std::ranges::find(discarded, free) != discarded.end());
		CHECK(std::ranges::find(discarded, taken) == discarded.end());
		CHECK(Read(taken) == "made since");
		CHECK_FALSE(trash.CanUndo());
	};

	TEST_CASE("a file put back is not discarded")
	{
		const Project project;
		const std::string file = project.Write("Scene.loomscene", "kept");
		std::vector<std::string> discarded{ };

		{
			Loom::AssetTrash trash([&discarded](const std::string& path) { discarded.push_back(path); });

			REQUIRE(trash.Delete({ file }));
			REQUIRE(trash.Step(true));
		};

		CHECK(discarded.empty());
		CHECK(Read(file) == "kept");
	};

#ifdef _WIN32
	TEST_CASE("a folder on another volume leaves and comes back")
	{
		const Project project;
		project.Write("Folder/Inner.png", "inner");

		const std::filesystem::path share = OnAnotherVolume(project.root);

		if (share.empty())
			return;

		const std::string folder = (share / "Folder").string();

		Loom::AssetTrash trash([](const std::string&) { });

		REQUIRE(trash.Delete({ folder }));
		CHECK_FALSE(There(folder));

		REQUIRE(trash.Step(true));
		CHECK(Read((share / "Folder" / "Inner.png").string()) == "inner");
	};

	TEST_CASE("a folder on another volume that cannot finish leaving stays whole, with no step")
	{
		const Project project;
		project.Write("Folder/A Free.png", "free");
		const std::string held_open = project.Write("Folder/B Open.png", "open");

		const std::filesystem::path share = OnAnotherVolume(project.root);

		if (share.empty())
			return;

		std::string error{ };

		Loom::AssetTrash trash([](const std::string&) { });

		{
			std::ofstream open(held_open, std::ios::app);

			CHECK_FALSE(trash.Delete({ (share / "Folder").string() }, &error));
		};

		CHECK_FALSE(error.empty());
		CHECK_FALSE(trash.CanUndo());
		CHECK(Read((project.root / "Folder" / "A Free.png").string()) == "free");
		CHECK(Read(held_open) == "open");
	};
#endif
};
