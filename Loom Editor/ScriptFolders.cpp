#include "ScriptFolders.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>


namespace Loom
{
	namespace
	{
		bool IsAtOrUnder(const std::string& path, const std::string& folder)
		{
			return path == folder ||
				(path.size() > folder.size() &&
				path.compare(0, folder.size(), folder) == 0 &&
				path[folder.size()] == ScriptFolders::separator);
		};

		bool Alphabetical(const std::string& a, const std::string& b)
		{
			return std::ranges::lexicographical_compare(
				a,
				b,
				[](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
		};

		// The child of node called name, made if it is not there.
		ScriptFolders::Node& ChildOf(ScriptFolders::Node& node, const std::string& name)
		{
			for (ScriptFolders::Node& child : node.children)
				if (child.name == name)
					return child;

			const std::string path = node.path.empty() ? name : node.path + ScriptFolders::separator + name;

			return node.children.emplace_back(ScriptFolders::Node{ name, path });
		};

		ScriptFolders::Node& NodeAt(ScriptFolders::Node& root, const std::string& folder)
		{
			ScriptFolders::Node* node = &root;

			for (size_t start = 0; start < folder.size();)
			{
				size_t end = folder.find(ScriptFolders::separator, start);

				if (end == std::string::npos)
					end = folder.size();

				node = &ChildOf(*node, folder.substr(start, end - start));
				start = end + 1;
			};

			return *node;
		};

		void Sort(ScriptFolders::Node& node)
		{
			std::sort(
				node.children.begin(),
				node.children.end(),
				[](const ScriptFolders::Node& a, const ScriptFolders::Node& b) { return Alphabetical(a.name, b.name); });

			std::sort(node.scripts.begin(), node.scripts.end(), Alphabetical);

			for (ScriptFolders::Node& child : node.children)
				Sort(child);
		};
	};

	void ScriptFolders::Load(const std::string& file)
	{
		Clear();

		std::ifstream in(file);
		std::string line;

		const size_t folder_length = std::strlen(folder_key);
		const size_t script_length = std::strlen(script_prefix);

		while (std::getline(in, line))
		{
			while (!line.empty() && line.back() == '\r')
				line.pop_back();

			if (line.rfind(folder_key, 0) == 0)
			{
				const std::string folder = line.substr(folder_length);

				if (!folder.empty())
					m_folders.insert(folder);

				continue;
			};

			const size_t equals = line.find('=');

			if (line.rfind(script_prefix, 0) == 0 && equals != std::string::npos)
				Move(line.substr(script_length, equals - script_length), line.substr(equals + 1));
		};
	};

	bool ScriptFolders::Save(const std::string& file) const
	{
		std::ofstream out(file, std::ios::binary);

		for (const std::string& folder : m_folders)
			out << folder_key << folder << '\n';

		for (const auto& [script, folder] : m_scripts)
			out << script_prefix << script << '=' << folder << '\n';

		return (bool)out;
	};

	void ScriptFolders::Clear()
	{
		m_folders.clear();
		m_scripts.clear();
	};

	std::string ScriptFolders::FolderOf(const std::string& script) const
	{
		const auto found = m_scripts.find(script);

		return found != m_scripts.end() ? found->second : "";
	};

	void ScriptFolders::Move(const std::string& script, const std::string& folder)
	{
		if (script.empty())
			return;

		if (folder.empty())
		{
			m_scripts.erase(script);
			return;
		};

		m_scripts[script] = folder;
		m_folders.insert(folder);
	};

	std::string ScriptFolders::AddFolder(const std::string& parent, const std::string& name)
	{
		const std::string tidy = Tidy(name);

		if (tidy.empty() || m_folders.contains(Join(parent, tidy)))
			return "";

		const std::string path = Join(parent, tidy);

		m_folders.insert(path);

		return path;
	};

	std::string ScriptFolders::RenameFolder(const std::string& folder, const std::string& name)
	{
		const std::string tidy = Tidy(name);

		if (tidy.empty())
			return "";

		const std::string path = Join(ParentOf(folder), tidy);

		if (path == folder)
			return path;

		if (m_folders.contains(path))
			return "";

		Repath(folder, path);

		return path;
	};

	bool ScriptFolders::MoveFolder(const std::string& folder, const std::string& parent)
	{
		const std::string path = Join(parent, NameOf(folder));

		if (path == folder)
			return true;

		if (IsAtOrUnder(parent, folder) || m_folders.contains(path))
			return false;

		Repath(folder, path);

		return true;
	};

	void ScriptFolders::RemoveFolder(const std::string& folder)
	{
		const std::string parent = ParentOf(folder);

		for (auto& [script, path] : m_scripts)
			if (path == folder)
				path = parent;

		std::erase_if(m_scripts, [](const auto& entry) { return entry.second.empty(); });

		m_folders.erase(folder);

		// What is under it moves up a level, landing in a folder of the same
		// name when the level above already has one.
		const std::string prefix = folder + separator;

		std::set<std::string> folders{ };

		for (const std::string& path : m_folders)
			folders.insert(path.rfind(prefix, 0) == 0 ? Join(parent, path.substr(prefix.size())) : path);

		for (auto& [script, path] : m_scripts)
			if (path.rfind(prefix, 0) == 0)
				path = Join(parent, path.substr(prefix.size()));

		m_folders = std::move(folders);
	};

	ScriptFolders::Node ScriptFolders::Build(const std::vector<std::string>& scripts) const
	{
		Node root{ };

		for (const std::string& folder : m_folders)
			NodeAt(root, folder);

		for (const std::string& script : scripts)
			NodeAt(root, FolderOf(script)).scripts.push_back(script);

		Sort(root);

		return root;
	};

	std::string ScriptFolders::ParentOf(const std::string& folder)
	{
		const size_t last = folder.rfind(separator);

		return last == std::string::npos ? "" : folder.substr(0, last);
	};

	std::string ScriptFolders::NameOf(const std::string& folder)
	{
		return folder.substr(folder.rfind(separator) + 1);
	};

	std::string ScriptFolders::Tidy(const std::string& name)
	{
		constexpr const char* blank = " \t";

		const size_t first = name.find_first_not_of(blank);

		if (first == std::string::npos || name.find(separator) != std::string::npos)
			return "";

		return name.substr(first, name.find_last_not_of(blank) - first + 1);
	};

	std::string ScriptFolders::Join(const std::string& parent, const std::string& name)
	{
		return parent.empty() ? name : parent + separator + name;
	};

	void ScriptFolders::Repath(const std::string& from, const std::string& to)
	{
		const auto moved = [&](const std::string& path)
		{
			return to + path.substr(from.size());
		};

		std::set<std::string> folders{ };

		for (const std::string& path : m_folders)
			folders.insert(IsAtOrUnder(path, from) ? moved(path) : path);

		// A folder with nothing under it still has to come along.
		folders.insert(to);

		for (auto& [script, path] : m_scripts)
			if (IsAtOrUnder(path, from))
				path = moved(path);

		m_folders = std::move(folders);
	};
};
