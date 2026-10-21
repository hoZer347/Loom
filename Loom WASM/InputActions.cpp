#include "InputActions.h"

#include <algorithm>
#include <fstream>
#include <utility>


namespace Loom
{
	namespace
	{
		std::vector<InputActions::Action> actions{ };

		const InputActions::Action* Find(std::string_view name)
		{
			const auto found = std::find_if(actions.begin(), actions.end(), [name](const InputActions::Action& action) { return action.name == name; });

			return found != actions.end() ? &*found : nullptr;
		};

		bool Either(std::string_view name, bool (*asked)(Key))
		{
			const InputActions::Action* action = Find(name);

			return action != nullptr && (asked(action->key) || asked(action->alt));
		};

		std::string_view Trimmed(std::string_view text)
		{
			constexpr std::string_view blank = " \t\r";

			const size_t first = text.find_first_not_of(blank);

			return first == std::string_view::npos ? std::string_view{ } : text.substr(first, text.find_last_not_of(blank) - first + 1);
		};
	};

	bool InputActions::Down(std::string_view name)
	{
		return Either(name, &Input::GetKeyDown);
	};

	bool InputActions::Held(std::string_view name)
	{
		return Either(name, &Input::GetKey);
	};

	bool InputActions::Up(std::string_view name)
	{
		return Either(name, &Input::GetKeyUp);
	};

	const std::vector<InputActions::Action>& InputActions::All()
	{
		return actions;
	};

	void InputActions::Set(std::vector<Action> to)
	{
		actions = std::move(to);
	};

	void InputActions::Load(const std::string& file)
	{
		std::ifstream in(file);

		Set(Read(in));
	};

	bool InputActions::Save(const std::string& file)
	{
		std::ofstream out(file, std::ios::binary);

		Write(out, actions);

		return (bool)out;
	};

	std::vector<InputActions::Action> InputActions::Read(std::istream& in)
	{
		std::vector<Action> read;
		std::string line;

		while (std::getline(in, line))
		{
			const std::string_view text = line;
			const size_t equals = text.find('=');

			if (equals == std::string_view::npos)
				continue;

			const std::string_view name = Trimmed(text.substr(0, equals));
			const std::string_view keys = text.substr(equals + 1);
			const size_t comma = keys.find(',');

			const bool taken = std::any_of(read.begin(), read.end(), [name](const Action& action) { return action.name == name; });

			if (name.empty() || taken)
				continue;

			read.push_back({
				std::string(name),
				KeyNamed(Trimmed(keys.substr(0, comma))),
				comma == std::string_view::npos ? Key::None : KeyNamed(Trimmed(keys.substr(comma + 1))) });
		};

		return read;
	};

	void InputActions::Write(std::ostream& out, const std::vector<Action>& to_write)
	{
		for (const Action& action : to_write)
			out << action.name << '=' << NameOf(action.key) << ',' << NameOf(action.alt) << '\n';
	};
};
