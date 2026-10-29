#include "Executable.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif


namespace Loom
{
	std::filesystem::path ExecutablePath()
	{
#if defined(_WIN32)
		std::wstring path(MAX_PATH, L'\0');

		while (GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size()) == path.size())
			path.resize(path.size() * 2);

		return std::filesystem::path(path.c_str());
#elif defined(__EMSCRIPTEN__)
		return { };
#else
		std::error_code code;

		return std::filesystem::read_symlink("/proc/self/exe", code);
#endif
	};
};
