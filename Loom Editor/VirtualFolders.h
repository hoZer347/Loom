#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::VirtualFolders
	* - A panel's folders: a tree its items are filed in, by name, apart from
	*   wherever the items themselves live, so filing one moves nothing else.
	*   The Scripts tab files script types and shaders in them, the
	*   FileTabs files and the Key Bindings panel actions
	* - Folders are paths with '/' between nested ones. An item filed nowhere
	*   is at the top
	* - A panel that keeps them in a file writes folder=<path> and
	*   <item_prefix><item>=<path> lines
	*/
	struct VirtualFolders
	{
		static constexpr char separator = '/';
		static constexpr const char* folder_key = "folder=";

		// What a file of them puts between an item and its folder, so no
		// folder's name holds one.
		static constexpr char equals = '=';

		struct Node
		{
			std::string name{ };

			// From the top, empty for the top itself.
			std::string path{ };

			// Each alphabetical, the items by what follows their last separator.
			std::vector<Node> children{ };
			std::vector<std::string> items{ };
		};

		void Clear();

		// Empty when the file is not there yet.
		void Load(const std::string& file, const char* item_prefix);
		bool Save(const std::string& file, const char* item_prefix) const;

		// Every folder, each by its path.
		const std::set<std::string>& Folders() const { return m_folders; };

		// Where the item is filed: a folder's path, or empty for the top.
		std::string FolderOf(const std::string& item) const;

		// Files the item in folder, which is made if it is not there yet.
		void Move(const std::string& item, const std::string& folder);

		// A folder called name in parent, which is the top when empty. Its path,
		// or empty when the name is blank, holds a separator or '=', or is taken.
		std::string AddFolder(const std::string& parent, const std::string& name);

		// The same folder under a new name, holding what it held. Its new path,
		// or empty when the name cannot be used.
		std::string RenameFolder(const std::string& folder, const std::string& name);

		// Into parent, with everything in it. False when parent is the folder
		// itself or inside it, or already has one by that name.
		bool MoveFolder(const std::string& folder, const std::string& parent);

		// What the folder held goes to the one it was in.
		void RemoveFolder(const std::string& folder);

		// These items, filed into the folders. A folder an item is filed in is
		// there even when the folders left it out.
		Node Build(const std::vector<std::string>& items) const;

		// Whether path is folder or a folder inside it.
		static bool IsAtOrUnder(const std::string& path, const std::string& folder);

		// Whether a goes before b, whatever their case: the order Build files
		// folders and items in.
		static bool Alphabetical(std::string_view a, std::string_view b);

		// The folder path up to its last separator, or empty at the top.
		static std::string ParentOf(const std::string& folder);
		static std::string NameOf(const std::string& folder);

	protected:
		std::set<std::string> m_folders{ };
		std::map<std::string, std::string> m_items{ };

	private:
		// A trimmed name with no separator or '=' in it, or empty when there is none.
		static std::string Tidy(const std::string& name);

		static std::string Join(const std::string& parent, const std::string& name);

		// Gives from, and every folder and item under it, the path to instead.
		void Repath(const std::string& from, const std::string& to);
	};
};
