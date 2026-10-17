#include "FolderWatch.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


namespace Loom
{
	FolderWatch::~FolderWatch()
	{
		Watch("", false);
	};

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
};
