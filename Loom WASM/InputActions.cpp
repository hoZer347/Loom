#include "InputActions.h"

#include <algorithm>
#include <fstream>
#include <utility>


namespace Loom
{
	namespace
	{
		constexpr char name_end = '=';
		constexpr char folder_start = '[';
		constexpr char folder_end = ']';

		std::vector<InputActions::Action> actions{ };
		std::vector<std::string> folders{ };

		bool Either(std::string_view name, bool (*asked)(Key))
		{
			const InputActions::Action* action = InputActions::Find(name);

			return action != nullptr && (asked(action->key) || asked(action->alt));
		};

		std::string_view Trimmed(std::string_view text)
		{
			constexpr std::string_view blank = " \t\r";

			const size_t first = text.find_first_not_of(blank);

			return first == std::string_view::npos ? std::string_view{ } : text.substr(first, text.find_last_not_of(blank) - first + 1);
		};

		bool Contains(const std::vector<std::string>& folders_in, const std::string& folder)
		{
			return std::find(folders_in.begin(), folders_in.end(), folder) != folders_in.end();
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

	const std::vector<std::string>& InputActions::Folders()
	{
		return folders;
	};

	void InputActions::Set(std::vector<Action> to, std::vector<std::string> in)
	{
		actions = std::move(to);
		folders = std::move(in);
	};

	const InputActions::Action* InputActions::Find(std::string_view name)
	{
		const auto found = std::find_if(actions.begin(), actions.end(), [name](const Action& action) { return action.name == name; });

		return found != actions.end() ? &*found : nullptr;
	};

	std::string InputActions::Tidy(std::string_view name)
	{
		const std::string_view trimmed = Trimmed(name);

		return trimmed.find(name_end) == std::string_view::npos ? std::string(trimmed) : std::string{ };
	};

	void InputActions::Load(const std::string& file)
	{
		std::ifstream in(file);
		File read = Read(in);

		Set(std::move(read.actions), std::move(read.folders));
	};

	bool InputActions::Save(const std::string& file)
	{
		std::ofstream out(file, std::ios::binary);

		Write(out, { actions, folders });

		return (bool)out;
	};

	InputActions::File InputActions::Read(std::istream& in)
	{
		File read;
		std::string folder;
		std::string line;

		while (std::getline(in, line))
		{
			const std::string_view text = Trimmed(line);

			if (text.size() > 1 && text.front() == folder_start && text.back() == folder_end)
			{
				folder = Trimmed(text.substr(1, text.size() - 2));

				if (!folder.empty() && !Contains(read.folders, folder))
					read.folders.push_back(folder);

				continue;
			};

			const size_t equals = text.find(name_end);

			if (equals == std::string_view::npos)
				continue;

			const std::string name = Tidy(text.substr(0, equals));
			const std::string_view keys = text.substr(equals + 1);
			const size_t comma = keys.find(',');

			const bool taken = std::any_of(read.actions.begin(), read.actions.end(), [&name](const Action& action) { return action.name == name; });

			if (name.empty() || taken)
				continue;

			read.actions.push_back({
				name,
				KeyNamed(Trimmed(keys.substr(0, comma))),
				comma == std::string_view::npos ? Key::None : KeyNamed(Trimmed(keys.substr(comma + 1))),
				folder });
		};

		return read;
	};

	void InputActions::Write(std::ostream& out, const File& file)
	{
		// The file's folders in its order, then any an action is filed in that
		// it left out.
		std::vector<std::string> written = file.folders;

		for (const Action& action : file.actions)
			if (!action.folder.empty() && !Contains(written, action.folder))
				written.push_back(action.folder);

		const auto write = [&](const std::string& folder)
		{
			for (const Action& action : file.actions)
				if (action.folder == folder)
					out << action.name << name_end << NameOf(action.key) << ',' << NameOf(action.alt) << '\n';
		};

		write("");

		for (const std::string& folder : written)
		{
			out << folder_start << folder << folder_end << '\n';
			write(folder);
		};
	};
};
