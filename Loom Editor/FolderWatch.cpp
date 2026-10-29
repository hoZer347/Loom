#include "FolderWatch.h"

// The editor runs on Windows and Linux, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <filesystem>

#include <sys/inotify.h>
#include <unistd.h>
#endif


namespace Loom
{
	FolderWatch::~FolderWatch()
	{
		Watch("", false);
	};

#ifdef _WIN32
	void FolderWatch::Watch(const std::string& folder, bool writes)
	{
		if (m_handle != nullptr)
			FindCloseChangeNotification(m_handle);

		m_handle = nullptr;
		m_folder = folder;

		if (folder.empty())
			return;

		const DWORD names = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME;
		const HANDLE handle = FindFirstChangeNotificationA(
			folder.c_str(),
			TRUE,
			writes ? names | FILE_NOTIFY_CHANGE_LAST_WRITE : names);

		if (handle != INVALID_HANDLE_VALUE)
			m_handle = handle;
	};

	bool FolderWatch::Changed()
	{
		if (m_handle == nullptr || WaitForSingleObject(m_handle, 0) != WAIT_OBJECT_0)
			return false;

		FindNextChangeNotification(m_handle);

		return true;
	};
#else
	namespace
	{
		constexpr uint32_t NAMES = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF;
		constexpr uint32_t WRITES = IN_MODIFY | IN_CLOSE_WRITE;
		constexpr size_t EVENTS_SIZE = 64 * 1024;

		// inotify watches one folder each, so every folder under it gets its own.
		void WatchTree(int descriptor, const std::filesystem::path& folder, uint32_t mask)
		{
			std::error_code code;

			inotify_add_watch(descriptor, folder.c_str(), mask | IN_ONLYDIR);

			for (auto entry = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, code);
				entry != std::filesystem::recursive_directory_iterator();
				entry.increment(code))
				if (entry->is_directory(code) && !entry->is_symlink(code))
					inotify_add_watch(descriptor, entry->path().c_str(), mask | IN_ONLYDIR);
		};
	};

	void FolderWatch::Watch(const std::string& folder, bool writes)
	{
		if (m_descriptor >= 0)
			close(m_descriptor);

		m_descriptor = -1;
		m_folder = folder;

		if (folder.empty())
			return;

		m_mask = writes ? NAMES | WRITES : NAMES;
		m_descriptor = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

		if (m_descriptor >= 0)
			WatchTree(m_descriptor, folder, m_mask);
	};

	bool FolderWatch::Changed()
	{
		if (m_descriptor < 0)
			return false;

		alignas(inotify_event) char events[EVENTS_SIZE];
		bool changed = false;
		bool new_folders = false;
		ssize_t length = 0;

		while ((length = read(m_descriptor, events, sizeof(events))) > 0)
		{
			changed = true;

			for (const char* at = events; at < events + length; )
			{
				const inotify_event* event = (const inotify_event*)at;

				if ((event->mask & IN_ISDIR) && (event->mask & (IN_CREATE | IN_MOVED_TO)))
					new_folders = true;

				at += sizeof(inotify_event) + event->len;
			};
		};

		if (new_folders)
			WatchTree(m_descriptor, m_folder, m_mask);

		return changed;
	};
#endif
};

#endif
