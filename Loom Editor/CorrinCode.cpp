#include "CorrinCode.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <cstdlib>
#include <system_error>


namespace Loom
{
	namespace
	{
		// Environment variable naming a Corrin Code exe to launch instead of the installed one.
		const char* const override_variable = "CORRIN_CODE_EXE";

		// Environment variable holding the folder Corrin Code installs under.
		const char* const local_app_data_variable = "LOCALAPPDATA";

		// Corrin Code's install folder under local app data.
		const char* const install_folder = "Corrin Code";

		// Corrin Code's exe in its install folder.
		const char* const exe_name = "Corrin Code.exe";

		bool IsFile(const std::filesystem::path& path)
		{
			std::error_code code;
			return std::filesystem::is_regular_file(path, code);
		};

		std::string Environment(const char* name)
		{
#ifdef _WIN32
			char* value = nullptr;
			size_t length = 0;

			if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
				return "";

			const std::string result = value;
			std::free(value);

			return result;
#else
			const char* value = std::getenv(name);
			return value != nullptr ? value : "";
#endif
		};
	};

	std::filesystem::path CorrinCode::Find(const std::string& override_path, const std::filesystem::path& local_app_data)
	{
		if (!override_path.empty() && IsFile(override_path))
			return override_path;

		if (local_app_data.empty())
			return { };

		const std::filesystem::path installed = local_app_data / install_folder / exe_name;
		return IsFile(installed) ? installed : std::filesystem::path();
	};

	std::filesystem::path CorrinCode::Locate()
	{
		return Find(Environment(override_variable), Environment(local_app_data_variable));
	};

	bool CorrinCode::Open(const std::filesystem::path& exe, const std::filesystem::path& project_folder)
	{
#ifdef _WIN32
		std::wstring command = L"\"" + exe.wstring() + L"\" \"" + project_folder.wstring() + L"\"";

		STARTUPINFOW startup{ sizeof(startup) };
		PROCESS_INFORMATION process{ };

		if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &startup, &process))
			return false;

		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);

		return true;
#else
		(void)exe;
		(void)project_folder;
		return false;
#endif
	};
};
