#include "EditorLayouts.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>


namespace Loom
{
	namespace
	{
		std::filesystem::path PathOf(const std::filesystem::path& folder, const std::string& name)
		{
			return folder / (name + EditorLayouts::Extension);
		};
	};

	bool EditorLayouts::IsValidName(const std::string& name)
	{
		if (name.empty() || name.size() > MaxNameLength || name.front() == ' ' || name.back() == ' ')
			return false;

		for (const char c : name)
			if (!isalnum((unsigned char)c) && c != ' ' && c != '_' && c != '-')
				return false;

		return true;
	};

	std::vector<std::string> EditorLayouts::Names(const std::filesystem::path& folder)
	{
		std::vector<std::string> names;
		std::error_code error;

		for (const auto& entry : std::filesystem::directory_iterator(folder, error))
			if (entry.is_regular_file(error) && entry.path().extension() == Extension)
				names.push_back(entry.path().stem().string());

		std::sort(names.begin(), names.end());

		return names;
	};

	bool EditorLayouts::Save(const std::filesystem::path& folder, const std::string& name)
	{
		if (!IsValidName(name))
			return false;

		std::error_code error;
		std::filesystem::create_directories(folder, error);

		size_t size = 0;
		const char* settings = ImGui::SaveIniSettingsToMemory(&size);

		std::ofstream file(PathOf(folder, name), std::ios::binary);
		file.write(settings, (std::streamsize)size);

		return file.good();
	};

	bool EditorLayouts::Load(const std::filesystem::path& folder, const std::string& name)
	{
		std::ifstream file(PathOf(folder, name), std::ios::binary);

		if (!file)
			return false;

		const std::string settings{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

		ImGui::LoadIniSettingsFromMemory(settings.c_str(), settings.size());

		// What was loaded is the layout the editor reopens with.
		ImGui::MarkIniSettingsDirty();

		return true;
	};

	bool EditorLayouts::Delete(const std::filesystem::path& folder, const std::string& name)
	{
		std::error_code error;

		return std::filesystem::remove(PathOf(folder, name), error);
	};
};
