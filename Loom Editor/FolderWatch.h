#pragma once

#include <string>


namespace Loom
{
	/**
	* Loom::FolderWatch
	* - A change notification on a folder and everything under it, so the
	*   editor rescans when something there changes rather than on a timer
	*/
	struct FolderWatch final
	{
		FolderWatch() = default;
		~FolderWatch();

		FolderWatch(const FolderWatch&) = delete;
		FolderWatch& operator=(const FolderWatch&) = delete;

		// Lets go of the folder watched before. Names alone notice files and
		// folders being added, removed or renamed; writes also notice a file
		// being saved. An empty folder watches nothing.
		void Watch(const std::string& folder, bool writes);

		const std::string& GetFolder() const { return m_folder; };

		// Whether anything has changed since the last call.
		bool Changed();

	private:
		void* m_handle = nullptr;
		std::string m_folder{ };
	};
};
