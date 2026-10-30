#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::ExtensionLibrary
	* - The extensions the editor loaded. Each library in the folder loads from
	*   a copy, so a rebuild can replace it while editors hold it open
	* - One that fails to load is reported to the console and skipped
	*/
	struct ExtensionLibrary final
	{
		ExtensionLibrary() = default;
		ExtensionLibrary(const ExtensionLibrary&) = delete;
		ExtensionLibrary& operator=(const ExtensionLibrary&) = delete;
		~ExtensionLibrary() { Unload(); };

		// A folder for the user's extensions, shared by every editor on the machine.
		static std::filesystem::path UserFolder();

		void Load(const std::filesystem::path& folder);

		// Stops every extension, removes their panels and frees the libraries.
		void Unload();

		size_t Count() const { return m_loaded.size(); };

		// What a library exports.
		struct Entry
		{
			int (*abi)() = nullptr;
			void (*start)() = nullptr;
			void (*stop)() = nullptr;
		};

		// Starts an extension and keeps it until Unload. If refused, gives why,
		// and any panel it added is gone.
		std::optional<std::string> Admit(const Entry& entry, void* module, const std::filesystem::path& copy);

	private:
		void Open(const std::filesystem::path& library);

		struct Loaded
		{
			void* module = nullptr;
			void (*stop)() = nullptr;
			std::filesystem::path copy;
		};

		std::vector<Loaded> m_loaded{ };
	};
};
