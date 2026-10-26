#pragma once

#include <map>
#include <ostream>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::ComponentMenu
	* - What the Add Component menu offers, sorted into categories: the one the
	*   project files a component under, else the one its type declares, else
	*   Custom for a type from the project's scripts
	* - A project's categories are category.<component>=<category> lines in its
	*   .loomproject, with '/' between nested categories
	*/
	struct ComponentMenu final
	{
		#pragma region Categories

		static constexpr const char* custom_category = "Custom";
		static constexpr char separator = '/';

		// "Rendering / 2D/" is Rendering/2D.
		static std::string Tidy(const std::string& category);

		void Clear() { m_categories.clear(); };

		// Whether the project files the component somewhere of its own choosing.
		bool IsFiled(const std::string& component) const { return m_categories.contains(component); };

		// Files a component under category, or under its own again when
		// category is empty.
		void Set(const std::string& component, const std::string& category);

		// Where the menu puts a registered component; empty for the top level.
		std::string CategoryOf(const std::string& component, bool script) const;

		#pragma endregion

		#pragma region Project File

		static constexpr const char* key_prefix = "category.";

		// Takes a line of the project file, if it is one of these.
		void Read(const std::string& line);
		void Write(std::ostream& out) const;

		// Rewrites the category lines of a project file and keeps the rest.
		// False when it could not be read or written.
		bool Save(const std::string& project_file) const;

		#pragma endregion

		#pragma region Menu

		struct Node
		{
			std::string name{ };

			// Custom first, the rest alphabetical.
			std::vector<Node> children{ };
			std::vector<std::string> components{ };
		};

		// Every registered component, scripts being the ones the project's
		// library registered.
		Node Build(const std::vector<std::string>& scripts) const;

		#pragma endregion

	private:
		std::map<std::string, std::string> m_categories{ };
	};
};
