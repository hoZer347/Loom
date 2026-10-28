#pragma once

#include <filesystem>
#include <span>
#include <string>


namespace Loom
{
	/**
	* Loom::FileTypes
	* - The editor's own files, which double-clicking in Explorer opens in the
	*   main checkout's editor
	* - Registered per user, so it needs no elevation
	*/
	struct FileTypes final
	{
		// An extension and the Explorer type, a ProgID, that opens it.
		struct Type
		{
			const char* extension;
			const wchar_t* progId;
			const wchar_t* description;
		};

		// Where Explorer reads the current user's file types, under HKEY_CURRENT_USER.
		static constexpr const wchar_t* UserClasses = L"Software\\Classes";

		// Every file of the editor's own, each with a type of its own.
		static std::span<const Type> All();

		// Whether the editor at that path may take Explorer's file types: only
		// the Release one built in an engine's main checkout, not in a git
		// worktree such as an agent's slot, nor a copy outside any engine.
		static bool MayClaim(const std::filesystem::path& executable);

		// Points every type under classes at the editor, and false when it may
		// not or a type could not be written. Explorer is told only when its
		// own types changed.
		static bool Claim(const std::filesystem::path& executable, const std::wstring& classes = UserClasses);

		// The command a file with that extension opens with under classes,
		// empty when there is none.
		static std::wstring OpenCommand(const char* extension, const std::wstring& classes = UserClasses);
	};
};
