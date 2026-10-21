#include "InputActions.h"

#include <algorithm>
#include <fstream>
#include <utility>


namespace Loom
{
	namespace
	{
		constexpr char name_end = '=';
		constexpr char key_join = '+';
		constexpr char folder_start = '[';
		constexpr char folder_end = ']';

		// What split a key from its alternate in a file before actions shared names.
		constexpr char older_alternate = ',';

		std::vector<InputActions::Action> actions{ };
		std::vector<std::string> folders{ };

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

		bool IsSet(Key key)
		{
			return key != Key::None;
		};

		// Whether the keys chosen are all held, and some of them made this
		// frame's edge: went down for Down, came up for Up.
		bool Together(const std::vector<Key>& keys, bool (*edge)(Key))
		{
			bool any = false;
			bool made = edge == nullptr;

			for (const Key key : keys)
			{
				if (!IsSet(key))
					continue;

				any = true;

				const bool edged = edge != nullptr && edge(key);

				if (!edged && !Input::GetKey(key))
					return false;

				made = made || edged;
			};

			return any && made;
		};

		bool Done(std::string_view name, bool (*edge)(Key))
		{
			return std::any_of(actions.begin(), actions.end(), [name, edge](const InputActions::Action& action) { return action.name == name && Together(action.keys, edge); });
		};

		// The keys of a part of a line, Key+Key, those that are no key left out.
		std::vector<Key> KeysIn(std::string_view part)
		{
			std::vector<Key> keys;

			while (!part.empty())
			{
				const size_t join = part.find(key_join);

				if (const Key key = KeyNamed(Trimmed(part.substr(0, join))); IsSet(key))
					keys.push_back(key);

				part = join == std::string_view::npos ? std::string_view{ } : part.substr(join + 1);
			};

			return keys;
		};
	};

	bool InputActions::Down(std::string_view name)
	{
		return Done(name, &Input::GetKeyDown);
	};

	bool InputActions::Held(std::string_view name)
	{
		return Done(name, nullptr);
	};

	bool InputActions::Up(std::string_view name)
	{
		return Done(name, &Input::GetKeyUp);
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

			if (name.empty())
				continue;

			std::string_view rest = text.substr(equals + 1);
			bool read_any = false;

			while (!rest.empty())
			{
				const size_t comma = rest.find(older_alternate);

				if (std::vector<Key> keys = KeysIn(rest.substr(0, comma)); !keys.empty())
				{
					read.actions.push_back({ name, std::move(keys), folder });
					read_any = true;
				};

				rest = comma == std::string_view::npos ? std::string_view{ } : rest.substr(comma + 1);
			};

			if (!read_any)
				read.actions.push_back({ name, { }, folder });
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
			{
				if (action.folder != folder)
					continue;

				out << action.name << name_end;

				bool first = true;

				for (const Key key : action.keys)
					if (IsSet(key))
					{
						out << (first ? "" : std::string(1, key_join)) << NameOf(key);
						first = false;
					};

				out << (first ? NameOf(Key::None) : "") << '\n';
			};
		};

		write("");

		for (const std::string& folder : written)
		{
			out << folder_start << folder << folder_end << '\n';
			write(folder);
		};
	};
};
