#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::ScriptFolders
	* - The Scripts panel's folders: a tree the project's script types are
	*   filed in, apart from the headers they are written in, so filing one
	*   moves no file and one header's types can sit in different folders
	* - A project keeps them beside its .loomproject in a .loomscripts file of
	*   the same name, as folder=<path> and script.<type>=<path> lines, with
	*   '/' between nested folders. A type filed nowhere is at the top
	*/
	struct ScriptFolders final
	{
		static constexpr const char* extension = ".loomscripts";
		static constexpr const char* folder_key = "folder=";
		static constexpr const char* script_prefix = "script.";
		static constexpr char separator = '/';

		struct Node
		{
			std::string name{ };

			// From the top, empty for the top itself.
			std::string path{ };

			// Each alphabetical.
			std::vector<Node> children{ };
			std::vector<std::string> scripts{ };
		};

		// Empty when the file is not there yet.
		void Load(const std::string& file);
		bool Save(const std::string& file) const;
		void Clear();

		// Where the type is filed: a folder's path, or empty for the top.
		std::string FolderOf(const std::string& script) const;

		// Files the type in folder, which is made if it is not there yet.
		void Move(const std::string& script, const std::string& folder);

		// A folder called name in parent, which is the top when empty. Its path,
		// or empty when the name is blank, holds a separator or is taken.
		std::string AddFolder(const std::string& parent, const std::string& name);

		// The same folder under a new name, holding what it held. Its new path,
		// or empty when the name cannot be used.
		std::string RenameFolder(const std::string& folder, const std::string& name);

		// Into parent, with everything in it. False when parent is the folder
		// itself or inside it, or already has one by that name.
		bool MoveFolder(const std::string& folder, const std::string& parent);

		// What the folder held goes to the one it was in.
		void RemoveFolder(const std::string& folder);

		// These scripts, filed into the folders. A folder a script is filed in
		// is there even when the file left it out.
		Node Build(const std::vector<std::string>& scripts) const;

		// The folder path up to its last separator, or empty at the top.
		static std::string ParentOf(const std::string& folder);
		static std::string NameOf(const std::string& folder);

	private:
		// A trimmed name with no separator in it, or empty when there is none.
		static std::string Tidy(const std::string& name);

		static std::string Join(const std::string& parent, const std::string& name);

		// Gives from, and every folder and script under it, the path to instead.
		void Repath(const std::string& from, const std::string& to);

		std::set<std::string> m_folders{ };
		std::map<std::string, std::string> m_scripts{ };
	};
};
