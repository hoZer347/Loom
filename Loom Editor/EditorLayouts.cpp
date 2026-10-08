#include "EditorLayouts.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cstring>
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

		// Names Windows opens as a device, whatever the extension.
		bool IsDeviceName(const std::string& name)
		{
			constexpr const char* devices[] = { "CON", "PRN", "AUX", "NUL" };
			constexpr const char* numbered[] = { "COM", "LPT" };
			constexpr size_t numbered_length = 4;

			for (const char* device : devices)
				if (EditorLayouts::SameName(name, device))
					return true;

			for (const char* prefix : numbered)
				if (name.size() == numbered_length &&
					EditorLayouts::SameName(name.substr(0, numbered_length - 1), prefix) &&
					isdigit((unsigned char)name.back()))
					return true;

			return false;
		};
	};

	bool EditorLayouts::IsValidName(const std::string& name)
	{
		if (name.empty() || name.size() > MaxNameLength || name.front() == ' ' || name.back() == ' ')
			return false;

		for (const char c : name)
			if (!isalnum((unsigned char)c) && c != ' ' && c != '_' && c != '-')
				return false;

		return !IsDeviceName(name);
	};

	bool EditorLayouts::SameName(const std::string& a, const std::string& b)
	{
		return std::equal(
			a.begin(), a.end(),
			b.begin(), b.end(),
			[](char x, char y) { return tolower((unsigned char)x) == tolower((unsigned char)y); });
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

	bool EditorLayouts::Load(const std::filesystem::path& folder, const std::string& name, std::vector<ImGuiID>* windows)
	{
		constexpr const char* window_entry = "[Window][";

		std::ifstream file(PathOf(folder, name), std::ios::binary);

		if (!file)
			return false;

		const std::string settings{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

		ImGui::LoadIniSettingsFromMemory(settings.c_str(), settings.size());

		if (windows)
		{
			windows->clear();

			for (size_t at = settings.find(window_entry); at != std::string::npos; at = settings.find(window_entry, at))
			{
				at += strlen(window_entry);

				const size_t end = settings.find("]\n", at);

				if (end == std::string::npos)
					break;

				windows->push_back(ImHashStr(settings.c_str() + at, end - at));
			};
		};

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
