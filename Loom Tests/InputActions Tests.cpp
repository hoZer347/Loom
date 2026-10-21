#include "doctest.h"

#include "Input.h"
#include "InputActions.h"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>


TEST_SUITE("InputActions")
{
	using Action = Loom::InputActions::Action;
	using Loom::Key;

	// Nothing held and no edges pending, and the actions as given.
	struct Fresh
	{
		explicit Fresh(std::vector<Action> actions = { })
		{
			for (int key = 0; key < Loom::Input::keys; key++)
				Loom::Input::OnKey(key, false);

			Loom::Input::Tick();
			Loom::Input::Tick();
			Loom::InputActions::Set(std::move(actions));
		};

		~Fresh()
		{
			Loom::InputActions::Set({ });
		};

		static void Press(Key key, bool down)
		{
			Loom::Input::OnKey((int)key, down);
			Loom::Input::Tick();
		};
	};

	TEST_CASE("every key's name is the key")
	{
		const auto keys = Loom::AllKeys();

		REQUIRE(!keys.empty());
		CHECK(keys.front() == Key::None);
		CHECK(std::is_sorted(keys.begin(), keys.end()));
		CHECK(std::adjacent_find(keys.begin(), keys.end()) == keys.end());

		for (const Key key : keys)
			CHECK(Loom::KeyNamed(Loom::NameOf(key)) == key);

		CHECK(Loom::NameOf(Key::LeftControl) == "LeftControl");
		CHECK(Loom::KeyNamed("Space") == Key::Space);
		CHECK(Loom::KeyNamed("No Such Key") == Key::None);
	};

	TEST_CASE("a file reads as its actions, keys by name and held together by +")
	{
		std::istringstream in(
			"Slowdown=Space\r\n"
			" Zoom In = LeftControl + Equal \n"
			"=A\n"
			"no equals here\n"
			"Slowdown=P\n"
			"Odd=Nonsense\n");

		const std::vector<Action> read = Loom::InputActions::Read(in).actions;

		REQUIRE(read.size() == 4);
		CHECK(read[0] == Action{ "Slowdown", { Key::Space } });
		CHECK(read[1] == Action{ "Zoom In", { Key::LeftControl, Key::Equal } });
		CHECK(read[2] == Action{ "Slowdown", { Key::P } });
		CHECK(read[3] == Action{ "Odd", { } });
	};

	TEST_CASE("an older file's key and alternate read as two actions by one name")
	{
		std::istringstream in("Pan Up=W,Up\nSlowdown=Space,None\n");

		const std::vector<Action> read = Loom::InputActions::Read(in).actions;

		REQUIRE(read.size() == 3);
		CHECK(read[0] == Action{ "Pan Up", { Key::W } });
		CHECK(read[1] == Action{ "Pan Up", { Key::Up } });
		CHECK(read[2] == Action{ "Slowdown", { Key::Space } });
	};

	TEST_CASE("a name is trimmed, and one blank or holding '=' is none")
	{
		CHECK(Loom::InputActions::Tidy("  Pan Up\t") == "Pan Up");
		CHECK(Loom::InputActions::Tidy("   ").empty());
		CHECK(Loom::InputActions::Tidy("Red=1").empty());
	};

	TEST_CASE("what is written reads back the same, folders and all")
	{
		const Loom::InputActions::File file = {
			{
				{ "Slowdown", { Key::Space } },
				{ "Pan Up", { Key::W }, "Camera" },
				{ "Zoom In", { Key::LeftControl, Key::Equal }, "Camera/Zoom" },
				{ "Unbound", { } } },
			{ "Camera", "Camera/Zoom", "Empty" } };

		std::ostringstream out;
		Loom::InputActions::Write(out, file);

		std::istringstream in(out.str());
		const Loom::InputActions::File read = Loom::InputActions::Read(in);

		CHECK(read.folders == file.folders);

		// Written grouped by folder, the top's first.
		REQUIRE(read.actions.size() == file.actions.size());
		CHECK(read.actions[0] == file.actions[0]);
		CHECK(read.actions[1] == file.actions[3]);
		CHECK(read.actions[2] == file.actions[1]);
		CHECK(read.actions[3] == file.actions[2]);
	};

	TEST_CASE("an action's folder is the heading above it, which play ignores")
	{
		Fresh fresh;

		std::istringstream in(
			"Slowdown=Space\n"
			"[ Camera ]\n"
			"Pan Up=W\n");

		const Loom::InputActions::File read = Loom::InputActions::Read(in);

		REQUIRE(read.actions.size() == 2);
		CHECK(read.actions[0].folder.empty());
		CHECK(read.actions[1].folder == "Camera");
		CHECK(read.folders == std::vector<std::string>{ "Camera" });

		Loom::InputActions::Set(read.actions, read.folders);
		Fresh::Press(Key::W, true);

		CHECK(Loom::InputActions::Down("Pan Up"));
	};

	TEST_CASE("any action by a name does it, and an unknown name never")
	{
		Fresh fresh({ { "Pan Up", { Key::W } }, { "Pan Up", { Key::Up } } });

		Fresh::Press(Key::Up, true);

		CHECK(Loom::InputActions::Down("Pan Up"));
		CHECK(Loom::InputActions::Held("Pan Up"));
		CHECK_FALSE(Loom::InputActions::Held("Pan Down"));

		Fresh::Press(Key::Up, false);

		CHECK(Loom::InputActions::Up("Pan Up"));
		CHECK_FALSE(Loom::InputActions::Held("Pan Up"));
	};

	TEST_CASE("keys held together go down when the last of them does")
	{
		Fresh fresh({ { "Zoom In", { Key::LeftControl, Key::Equal } } });

		Fresh::Press(Key::Equal, true);

		CHECK_FALSE(Loom::InputActions::Down("Zoom In"));

		Fresh::Press(Key::Equal, false);
		Fresh::Press(Key::LeftControl, true);

		CHECK_FALSE(Loom::InputActions::Down("Zoom In"));

		Fresh::Press(Key::Equal, true);

		CHECK(Loom::InputActions::Down("Zoom In"));
		CHECK(Loom::InputActions::Held("Zoom In"));

		Fresh::Press(Key::LeftControl, false);

		CHECK(Loom::InputActions::Up("Zoom In"));
		CHECK_FALSE(Loom::InputActions::Held("Zoom In"));
	};

	TEST_CASE("an action with no key chosen is never done")
	{
		Fresh fresh({ { "Unbound", { Key::None } } });

		Fresh::Press(Key::Space, true);

		CHECK_FALSE(Loom::InputActions::Down("Unbound"));
		CHECK_FALSE(Loom::InputActions::Held("Unbound"));
	};

	TEST_CASE("saved actions load back, and a missing file loads none")
	{
		Fresh fresh({ { "Slowdown", { Key::Space } } });

		const std::filesystem::path folder = std::filesystem::temp_directory_path() / "Loom InputActions Tests";
		const std::string file = (folder / "Project.loominput").string();

		std::filesystem::create_directories(folder);

		REQUIRE(Loom::InputActions::Save(file));

		Loom::InputActions::Set({ });
		Loom::InputActions::Load(file);

		REQUIRE(Loom::InputActions::All().size() == 1);
		CHECK(Loom::InputActions::All()[0].keys == std::vector<Key>{ Key::Space });

		std::filesystem::remove_all(folder);
		Loom::InputActions::Load(file);

		CHECK(Loom::InputActions::All().empty());
	};
};
