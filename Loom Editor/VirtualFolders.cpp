#include "VirtualFolders.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>


namespace Loom
{
	namespace
	{
		bool Alphabetical(const std::string& a, const std::string& b)
		{
			return std::ranges::lexicographical_compare(
				a,
				b,
				[](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
		};

		// The child of node called name, made if it is not there.
		VirtualFolders::Node& ChildOf(VirtualFolders::Node& node, const std::string& name)
		{
			for (VirtualFolders::Node& child : node.children)
				if (child.name == name)
					return child;

			const std::string path = node.path.empty() ? name : node.path + VirtualFolders::separator + name;

			return node.children.emplace_back(VirtualFolders::Node{ name, path });
		};

		VirtualFolders::Node& NodeAt(VirtualFolders::Node& root, const std::string& folder)
		{
			VirtualFolders::Node* node = &root;

			for (size_t start = 0; start < folder.size();)
			{
				size_t end = folder.find(VirtualFolders::separator, start);

				if (end == std::string::npos)
					end = folder.size();

				node = &ChildOf(*node, folder.substr(start, end - start));
				start = end + 1;
			};

			return *node;
		};

		void Sort(VirtualFolders::Node& node)
		{
			std::sort(
				node.children.begin(),
				node.children.end(),
				[](const VirtualFolders::Node& a, const VirtualFolders::Node& b) { return Alphabetical(a.name, b.name); });

			std::sort(node.items.begin(), node.items.end(), Alphabetical);

			for (VirtualFolders::Node& child : node.children)
				Sort(child);
		};
	};

	void VirtualFolders::Clear()
	{
		m_folders.clear();
		m_items.clear();
	};

	void VirtualFolders::Load(const std::string& file, const char* item_prefix)
	{
		Clear();

		std::ifstream in(file);
		std::string line;

		const size_t folder_length = std::strlen(folder_key);
		const size_t item_length = std::strlen(item_prefix);

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

			// Folders hold no '=', and file paths can.
			const size_t split = line.rfind(equals);

			if (line.rfind(item_prefix, 0) == 0 && split != std::string::npos)
				Move(line.substr(item_length, split - item_length), line.substr(split + 1));
		};
	};

	bool VirtualFolders::Save(const std::string& file, const char* item_prefix) const
	{
		std::ofstream out(file, std::ios::binary);

		for (const std::string& folder : m_folders)
			out << folder_key << folder << '\n';

		for (const auto& [item, folder] : m_items)
			out << item_prefix << item << equals << folder << '\n';

		return (bool)out;
	};

	std::string VirtualFolders::FolderOf(const std::string& item) const
	{
		const auto found = m_items.find(item);

		return found != m_items.end() ? found->second : "";
	};

	void VirtualFolders::Move(const std::string& item, const std::string& folder)
	{
		if (item.empty())
			return;

		if (folder.empty())
		{
			m_items.erase(item);
			return;
		};

		m_items[item] = folder;
		m_folders.insert(folder);
	};

	std::string VirtualFolders::AddFolder(const std::string& parent, const std::string& name)
	{
		const std::string tidy = Tidy(name);

		if (tidy.empty() || m_folders.contains(Join(parent, tidy)))
			return "";

		const std::string path = Join(parent, tidy);

		m_folders.insert(path);

		return path;
	};

	std::string VirtualFolders::RenameFolder(const std::string& folder, const std::string& name)
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

	bool VirtualFolders::MoveFolder(const std::string& folder, const std::string& parent)
	{
		const std::string path = Join(parent, NameOf(folder));

		if (path == folder)
			return true;

		if (IsAtOrUnder(parent, folder) || m_folders.contains(path))
			return false;

		Repath(folder, path);

		return true;
	};

	void VirtualFolders::RemoveFolder(const std::string& folder)
	{
		const std::string parent = ParentOf(folder);

		for (auto& [item, path] : m_items)
			if (path == folder)
				path = parent;

		std::erase_if(m_items, [](const auto& entry) { return entry.second.empty(); });

		m_folders.erase(folder);

		// What is under it moves up a level, landing in a folder of the same
		// name when the level above already has one.
		const std::string prefix = folder + separator;

		std::set<std::string> folders{ };

		for (const std::string& path : m_folders)
			folders.insert(path.rfind(prefix, 0) == 0 ? Join(parent, path.substr(prefix.size())) : path);

		for (auto& [item, path] : m_items)
			if (path.rfind(prefix, 0) == 0)
				path = Join(parent, path.substr(prefix.size()));

		m_folders = std::move(folders);
	};

	VirtualFolders::Node VirtualFolders::Build(const std::vector<std::string>& items) const
	{
		Node root{ };

		for (const std::string& folder : m_folders)
			NodeAt(root, folder);

		for (const std::string& item : items)
			NodeAt(root, FolderOf(item)).items.push_back(item);

		Sort(root);

		return root;
	};

	bool VirtualFolders::IsAtOrUnder(const std::string& path, const std::string& folder)
	{
		return path == folder ||
			(path.size() > folder.size() &&
			path.compare(0, folder.size(), folder) == 0 &&
			path[folder.size()] == separator);
	};

	std::string VirtualFolders::ParentOf(const std::string& folder)
	{
		const size_t last = folder.rfind(separator);

		return last == std::string::npos ? "" : folder.substr(0, last);
	};

	std::string VirtualFolders::NameOf(const std::string& folder)
	{
		return folder.substr(folder.rfind(separator) + 1);
	};

	std::string VirtualFolders::Tidy(const std::string& name)
	{
		constexpr const char* blank = " \t";

		const size_t first = name.find_first_not_of(blank);

		if (first == std::string::npos ||
			name.find(separator) != std::string::npos ||
			name.find(equals) != std::string::npos)
			return "";

		return name.substr(first, name.find_last_not_of(blank) - first + 1);
	};

	std::string VirtualFolders::Join(const std::string& parent, const std::string& name)
	{
		return parent.empty() ? name : parent + separator + name;
	};

	void VirtualFolders::Repath(const std::string& from, const std::string& to)
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

		for (auto& [item, path] : m_items)
			if (IsAtOrUnder(path, from))
				path = moved(path);

		m_folders = std::move(folders);
	};
};
