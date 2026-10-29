#include "FileTypes.h"

// The editor only runs on Windows, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "InputActions.h"
#include "Prefab.h"
#include "PreVertex.h"
#include "SceneSerializer.h"
#include "Shaders.h"

#include "BuildSettings.h"
#include "FileTab.h"
#include "ProjectTemplate.h"
#include "ScriptFolders.h"
#include "SpriteAsset.h"
#include "VisualStudio.h"

#include <windows.h>
#include <shlobj.h>

#include <iostream>

#pragma comment(lib, "shell32.lib")


namespace Loom
{
	namespace
	{
		constexpr FileTypes::Type TYPES[] =
		{
			{ ProjectTemplate::extension, L"Loom.Project", L"Loom Project" },
			{ SceneSerializer::extension, L"Loom.Scene", L"Loom Scene" },
			{ Prefab::extension, L"Loom.Prefab", L"Loom Prefab" },
			{ SpriteAsset::extension, L"Loom.Sprite", L"Loom Sprite" },
			{ Palette::extension, L"Loom.Palette", L"Loom Palette" },
			{ BuildSettings::extension, L"Loom.BuildSettings", L"Loom Build Settings" },
			{ ScriptFolders::extension, L"Loom.ScriptFolders", L"Loom Script Folders" },
			{ FileTabs::TwoD.extension, L"Loom.TwoDFolders", L"Loom 2D Folders" },
			{ FileTabs::ThreeD.extension, L"Loom.ThreeDFolders", L"Loom 3D Folders" },
			{ FileTabs::Sounds.extension, L"Loom.SoundFolders", L"Loom Sound Folders" },
			{ FileTabs::Fonts.extension, L"Loom.FontFolders", L"Loom Font Folders" },
			{ InputActions::extension, L"Loom.InputActions", L"Loom Key Bindings" },
			{ Shader::pixel_extension, L"Loom.PixelShader", L"Loom Pixel Shader" },
			{ Shader::compute_extension, L"Loom.ComputeShader", L"Loom Compute Shader" },
			{ PreVertex::extension, L"Loom.PreVertex", L"Loom Pre-Vertex Shader" },
			{ VisualStudio::header_extension, L"Loom.Header", L"C++ Header" },
		};

		constexpr const wchar_t* COMMAND_KEY = L"\\shell\\open\\command";

		std::wstring Wide(const char* text)
		{
			const std::string narrow = text;
			return std::wstring(narrow.begin(), narrow.end());
		};

		std::wstring ReadString(const std::wstring& key)
		{
			DWORD size = 0;

			if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS)
				return { };

			std::wstring value(size / sizeof(wchar_t), L'\0');

			if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS)
				return { };

			return value.c_str();
		};

		bool WriteString(const std::wstring& key, const wchar_t* name, const std::wstring& value)
		{
			return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ,
				value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
		};
	};

	std::span<const FileTypes::Type> FileTypes::All()
	{
		return TYPES;
	};

	bool FileTypes::MayClaim(const std::filesystem::path& executable)
	{
		// <engine>/x64/Release/Loom Editor.exe, the configuration games' scripts build for.
		const std::filesystem::path root = executable.parent_path().parent_path().parent_path();

		std::error_code code;

		return executable.parent_path() == root / "x64" / "Release" &&
			std::filesystem::exists(root / "Loom WASM" / "Loom.h", code) &&
			!ProjectTemplate::IsWorktree(root.string());
	};

	bool FileTypes::Claim(const std::filesystem::path& executable, const std::wstring& classes)
	{
		if (!MayClaim(executable))
			return false;

		const std::wstring command = L"\"" + executable.wstring() + L"\" \"%1\"";

		bool changed = false;
		bool failed = false;

		for (const Type& type : TYPES)
		{
			const std::wstring extension_key = classes + L"\\" + Wide(type.extension);
			const std::wstring prog_id_key = classes + L"\\" + type.progId;

			if (ReadString(extension_key) == type.progId && ReadString(prog_id_key + COMMAND_KEY) == command)
				continue;

			if (WriteString(extension_key, nullptr, type.progId) &&
				WriteString(extension_key + L"\\OpenWithProgids", type.progId, L"") &&
				WriteString(prog_id_key, nullptr, type.description) &&
				WriteString(prog_id_key + COMMAND_KEY, nullptr, command))
				changed = true;
			else
			{
				failed = true;
				std::cerr << "Could not register " << type.extension << " files with Explorer" << std::endl;
			};
		};

		if (changed && classes == UserClasses)
			SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

		return !failed;
	};

	std::wstring FileTypes::OpenCommand(const char* extension, const std::wstring& classes)
	{
		const std::wstring prog_id = ReadString(classes + L"\\" + Wide(extension));

		if (prog_id.empty())
			return { };

		return ReadString(classes + L"\\" + prog_id + COMMAND_KEY);
	};
};

#endif
