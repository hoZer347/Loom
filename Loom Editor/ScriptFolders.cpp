#include "ScriptFolders.h"

#include <cstring>
#include <fstream>


namespace Loom
{
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

		for (const auto& [script, folder] : m_items)
			out << script_prefix << script << '=' << folder << '\n';

		return (bool)out;
	};
};
