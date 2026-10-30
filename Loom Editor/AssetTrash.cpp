#include "AssetTrash.h"

#include <iostream>
#include <random>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cwchar>
#endif


namespace Loom
{
	namespace
	{
		// Rename says access denied, not cross device, for a folder on Windows.
		bool OnSameVolume(const std::filesystem::path& from, const std::filesystem::path& to)
		{
#ifdef _WIN32
			constexpr DWORD volume_length = MAX_PATH + 1;

			wchar_t from_volume[volume_length]{ };
			wchar_t to_volume[volume_length]{ };

			return GetVolumePathNameW(from.c_str(), from_volume, volume_length) &&
				GetVolumePathNameW(to.c_str(), to_volume, volume_length) &&
				_wcsicmp(from_volume, to_volume) == 0;
#else
			return true;
#endif
		};

		// On a failure from is whole again, even if it had started to go.
		void MoveAcrossVolumes(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& code)
		{
			std::filesystem::copy(from, to, std::filesystem::copy_options::recursive, code);

			if (!code)
				std::filesystem::remove_all(from, code);

			if (!code)
				return;

			std::error_code restoring;

			std::filesystem::copy(to, from, std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, restoring);

			if (!restoring)
				std::filesystem::remove_all(to, restoring);
		};

		bool MoveTo(const std::filesystem::path& from, const std::filesystem::path& to, std::string* error)
		{
			std::error_code code;

			std::filesystem::create_directories(to.parent_path(), code);

			bool across = !OnSameVolume(from, to.parent_path());

			if (!across)
			{
				std::filesystem::rename(from, to, code);
				across = code == std::errc::cross_device_link;
			};

			if (across)
				MoveAcrossVolumes(from, to, code);

			if (code && error)
				*error = "Could not move " + from.string() + ": " + code.message();

			return !code;
		};
	};

	void AssetTrash::Recycle(const std::string& path)
	{
#ifdef _WIN32
		// The list ends in a second terminator.
		std::wstring from = std::filesystem::path(path).wstring();
		from.push_back(L'\0');

		SHFILEOPSTRUCTW operation{ };
		operation.wFunc = FO_DELETE;
		operation.pFrom = from.c_str();
		operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

		if (SHFileOperationW(&operation) != 0)
			std::cerr << "Could not recycle " << path << std::endl;
#else
		std::error_code code;

		std::filesystem::remove_all(path, code);

		if (code)
			std::cerr << "Could not remove " << path << ": " << code.message() << std::endl;
#endif
	};

	bool AssetTrash::Delete(const std::vector<std::string>& paths, std::string* error)
	{
		if (m_folder.empty())
			m_folder = std::filesystem::temp_directory_path() / "Loom Trash" / std::to_string(std::random_device{}());

		std::vector<Entry> entries{ };

		for (const std::string& path : paths)
			entries.push_back({ path, (m_folder / std::to_string(m_taken++) / std::filesystem::path(path).filename()).string() });

		if (!Move(entries, true, error))
			return false;

		m_steps.Push(std::move(entries));

		return true;
	};

	std::vector<std::string> AssetTrash::Next(bool undo) const
	{
		std::vector<std::string> paths{ };

		if (const std::vector<Entry>* next = m_steps.Next(undo))
			for (const Entry& entry : *next)
				paths.push_back(entry.path);

		return paths;
	};

	bool AssetTrash::Step(bool undo, std::string* error)
	{
		const std::vector<Entry>* next = m_steps.Next(undo);

		if (next == nullptr)
			return false;

		std::vector<Entry> entries = *next;

		if (undo)
			for (const Entry& entry : entries)
				if (std::error_code code; std::filesystem::exists(entry.path, code))
				{
					if (error)
						*error = entry.path + " is back already";

					return false;
				};

		if (!Move(entries, !undo, error))
			return false;

		m_steps.Step(entries, undo);

		return true;
	};

	void AssetTrash::Empty()
	{
		for (const auto& [held, path] : m_held)
		{
			const std::filesystem::path original(path);
			std::error_code code;

			const bool free = std::filesystem::exists(original.parent_path(), code) && !std::filesystem::exists(original, code);

			m_discard(free && MoveTo(held, original, nullptr) ? path : held);
		};

		m_held.clear();
		m_steps.Clear();

		if (std::error_code code; !m_folder.empty())
			std::filesystem::remove_all(m_folder, code);
	};

	bool AssetTrash::Move(const std::vector<Entry>& entries, bool out, std::string* error)
	{
		for (size_t moved = 0; moved < entries.size(); moved++)
		{
			const Entry& entry = entries[moved];

			if (MoveTo(out ? entry.path : entry.held, out ? entry.held : entry.path, error))
				continue;

			while (moved-- > 0)
				MoveTo(out ? entries[moved].held : entries[moved].path, out ? entries[moved].path : entries[moved].held, nullptr);

			return false;
		};

		for (const Entry& entry : entries)
			if (out)
				m_held[entry.held] = entry.path;
			else m_held.erase(entry.held);

		return true;
	};
};
