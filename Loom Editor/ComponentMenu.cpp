#include "ComponentMenu.h"

#include "ComponentRegistry.h"

#include <algorithm>
#include <fstream>
#include <sstream>


namespace Loom
{
	namespace
	{
		const char* const blank = " \t";

		ComponentMenu::Node& ChildNamed(ComponentMenu::Node& node, const std::string& name)
		{
			const auto found = std::ranges::find(node.children, name, &ComponentMenu::Node::name);

			if (found != node.children.end())
				return *found;

			return node.children.emplace_back(ComponentMenu::Node{ name });
		};

		void Sort(ComponentMenu::Node& node)
		{
			std::ranges::sort(
				node.children,
				[](const ComponentMenu::Node& a, const ComponentMenu::Node& b)
				{
					const bool a_custom = a.name == ComponentMenu::custom_category;
					const bool b_custom = b.name == ComponentMenu::custom_category;

					return a_custom != b_custom
						? a_custom
						: a.name < b.name;
				});

			for (ComponentMenu::Node& child : node.children)
				Sort(child);
		};
	};

	#pragma region Categories

	std::string ComponentMenu::Tidy(const std::string& category)
	{
		std::string tidy, part;
		std::istringstream parts(category);

		while (std::getline(parts, part, separator))
		{
			const size_t first = part.find_first_not_of(blank);

			if (first == std::string::npos)
				continue;

			if (!tidy.empty())
				tidy += separator;

			tidy += part.substr(first, part.find_last_not_of(blank) - first + 1);
		};

		return tidy;
	};

	void ComponentMenu::Set(const std::string& component, const std::string& category)
	{
		if (component.empty())
			return;

		const std::string tidy = Tidy(category);

		if (tidy.empty())
			m_categories.erase(component);
		else
			m_categories[component] = tidy;
	};

	std::string ComponentMenu::CategoryOf(const std::string& component, bool script) const
	{
		if (const auto found = m_categories.find(component); found != m_categories.end())
			return found->second;

		if (const std::string declared = Tidy(ComponentRegistry::CategoryOf(component)); !declared.empty())
			return declared;

		return script
			? custom_category
			: "";
	};

	#pragma endregion

	#pragma region Project File

	void ComponentMenu::Read(const std::string& line)
	{
		if (line.rfind(key_prefix, 0) != 0)
			return;

		const size_t equals = line.find('=');

		if (equals == std::string::npos)
			return;

		const size_t prefix_length = std::char_traits<char>::length(key_prefix);

		Set(
			line.substr(prefix_length, equals - prefix_length),
			line.substr(equals + 1));
	};

	void ComponentMenu::Write(std::ostream& out) const
	{
		for (const auto& [component, category] : m_categories)
			out << key_prefix << component << '=' << category << '\n';
	};

	bool ComponentMenu::Save(const std::string& project_file) const
	{
		std::ifstream in(project_file);

		if (!in)
			return false;

		std::ostringstream kept;
		std::string line;

		while (std::getline(in, line))
		{
			while (!line.empty() && line.back() == '\r')
				line.pop_back();

			if (line.rfind(key_prefix, 0) != 0)
				kept << line << '\n';
		};

		in.close();

		Write(kept);

		std::ofstream out(project_file, std::ios::binary);
		out << kept.str();

		return (bool)out;
	};

	#pragma endregion

	#pragma region Menu

	ComponentMenu::Node ComponentMenu::Build(const std::vector<std::string>& scripts) const
	{
		Node root;

		for (const auto& [component, factory] : ComponentRegistry::All())
		{
			Node* node = &root;
			std::string part;
			std::istringstream parts(CategoryOf(component, std::ranges::find(scripts, component) != scripts.end()));

			while (std::getline(parts, part, separator))
				node = &ChildNamed(*node, part);

			node->components.push_back(component);
		};

		Sort(root);

		return root;
	};

	#pragma endregion
};
