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

		const std::vector<Action> read = Loom::InputActions::Read(in);

		REQUIRE(read.size() == 3);
		CHECK(read[0] == Action{ "Slowdown", Loom::Key::Space, Loom::Key::None });
		CHECK(read[1] == Action{ "Pan Up", Loom::Key::W, Loom::Key::Up });
		CHECK(read[2] == Action{ "Odd", Loom::Key::None, Loom::Key::None });
	};

	TEST_CASE("what is written reads back the same")
	{
		const std::vector<Action> actions = {
			{ "Slowdown", Loom::Key::Space, Loom::Key::None },
			{ "Add To Selection", Loom::Key::LeftControl, Loom::Key::RightControl } };

		std::ostringstream out;
		Loom::InputActions::Write(out, actions);

		std::istringstream in(out.str());

		CHECK(Loom::InputActions::Read(in) == actions);
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
