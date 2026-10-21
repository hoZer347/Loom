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
	};

	TEST_CASE("every key's name is the key")
	{
		const auto keys = Loom::AllKeys();

		REQUIRE(!keys.empty());
		CHECK(keys.front() == Loom::Key::None);
		CHECK(std::is_sorted(keys.begin(), keys.end()));
		CHECK(std::adjacent_find(keys.begin(), keys.end()) == keys.end());

		for (const Loom::Key key : keys)
			CHECK(Loom::KeyNamed(Loom::NameOf(key)) == key);

		CHECK(Loom::NameOf(Loom::Key::LeftControl) == "LeftControl");
		CHECK(Loom::KeyNamed("Space") == Loom::Key::Space);
		CHECK(Loom::KeyNamed("No Such Key") == Loom::Key::None);
	};

	TEST_CASE("a file reads as its actions, keys by name")
	{
		std::istringstream in(
			"Slowdown=Space,None\r\n"
			" Pan Up = W , Up \n"
			"=A,B\n"
			"no equals here\n"
			"Slowdown=P,None\n"
			"Odd=Nonsense\n");

		const std::vector<Action> read = Loom::InputActions::Read(in).actions;

		REQUIRE(read.size() == 3);
		CHECK(read[0] == Action{ "Slowdown", Loom::Key::Space, Loom::Key::None });
		CHECK(read[1] == Action{ "Pan Up", Loom::Key::W, Loom::Key::Up });
		CHECK(read[2] == Action{ "Odd", Loom::Key::None, Loom::Key::None });
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
				{ "Slowdown", Loom::Key::Space, Loom::Key::None },
				{ "Pan Up", Loom::Key::W, Loom::Key::Up, "Camera" },
				{ "Zoom In", Loom::Key::Equal, Loom::Key::None, "Camera/Zoom" },
				{ "Add To Selection", Loom::Key::LeftControl, Loom::Key::RightControl } },
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
			"Slowdown=Space,None\n"
			"[ Camera ]\n"
			"Pan Up=W,Up\n");

		const Loom::InputActions::File read = Loom::InputActions::Read(in);

		REQUIRE(read.actions.size() == 2);
		CHECK(read.actions[0].folder.empty());
		CHECK(read.actions[1].folder == "Camera");
		CHECK(read.folders == std::vector<std::string>{ "Camera" });

		Loom::InputActions::Set(read.actions, read.folders);
		Loom::Input::OnKey((int)Loom::Key::W, true);
		Loom::Input::Tick();

		CHECK(Loom::InputActions::Down("Pan Up"));
	};

	TEST_CASE("an action is done by either of its keys, and an unknown one never")
	{
		Fresh fresh({ { "Slowdown", Loom::Key::Space, Loom::Key::P } });

		Loom::Input::OnKey((int)Loom::Key::P, true);
		Loom::Input::Tick();

		CHECK(Loom::InputActions::Down("Slowdown"));
		CHECK(Loom::InputActions::Held("Slowdown"));
		CHECK_FALSE(Loom::InputActions::Up("Slowdown"));
		CHECK_FALSE(Loom::InputActions::Held("Pause"));

		Loom::Input::OnKey((int)Loom::Key::P, false);
		Loom::Input::Tick();

		CHECK(Loom::InputActions::Up("Slowdown"));
		CHECK_FALSE(Loom::InputActions::Held("Slowdown"));
	};

	TEST_CASE("saved actions load back, and a missing file loads none")
	{
		Fresh fresh({ { "Slowdown", Loom::Key::Space, Loom::Key::None } });

		const std::filesystem::path folder = std::filesystem::temp_directory_path() / "Loom InputActions Tests";
		const std::string file = (folder / "Project.loominput").string();

		std::filesystem::create_directories(folder);

		REQUIRE(Loom::InputActions::Save(file));

		Loom::InputActions::Set({ });
		Loom::InputActions::Load(file);

		REQUIRE(Loom::InputActions::All().size() == 1);
		CHECK(Loom::InputActions::All()[0].key == Loom::Key::Space);

		std::filesystem::remove_all(folder);
		Loom::InputActions::Load(file);

		CHECK(Loom::InputActions::All().empty());
	};
};
