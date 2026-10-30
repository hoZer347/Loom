#include "ExtensionLibrary.h"

#include "ExtensionRegistry.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <ranges>
#include <string>
#include <system_error>

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <Windows.h>
#else
	#include <dlfcn.h>
	#include <unistd.h>
#endif


namespace Loom
{
	namespace
	{
#ifdef _WIN32
		constexpr const char* library_extension = ".dll";
#else
		constexpr const char* library_extension = ".so";
#endif

		constexpr const char* copies_folder = "Loom Extensions";

		void* OpenModule(const std::filesystem::path& path)
		{
#ifdef _WIN32
			return LoadLibraryW(path.c_str());
#else
			return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
		};

		std::string LastError()
		{
#ifdef _WIN32
			return "error " + std::to_string(GetLastError());
#else
			return dlerror();
#endif
		};

		void* Symbol(void* module, const char* name)
		{
#ifdef _WIN32
			return (void*)GetProcAddress((HMODULE)module, name);
#else
			return dlsym(module, name);
#endif
		};

		void CloseModule(void* module)
		{
#ifdef _WIN32
			FreeLibrary((HMODULE)module);
#else
			dlclose(module);
#endif
		};

		unsigned long ProcessId()
		{
#ifdef _WIN32
			return GetCurrentProcessId();
#else
			return (unsigned long)getpid();
#endif
		};
	};

	std::filesystem::path ExtensionLibrary::UserFolder()
	{
#ifdef _WIN32
		wchar_t* base = nullptr;
		size_t length = 0;
		std::filesystem::path folder;

		if (_wdupenv_s(&base, &length, L"LOCALAPPDATA") == 0 && base != nullptr)
			folder = std::filesystem::path(base) / "Loom" / "Extensions";

		free(base);

		return folder;
#else
		const char* base = std::getenv("HOME");

		return base != nullptr
			? std::filesystem::path(base) / ".local" / "share" / "Loom" / "Extensions"
			: std::filesystem::path();
#endif
	};

	void ExtensionLibrary::Load(const std::filesystem::path& folder)
	{
		std::error_code code;
		const std::filesystem::path copies = std::filesystem::temp_directory_path(code) / copies_folder;

		// What a crashed editor left; a running editor's copies are locked and stay.
		for (const std::filesystem::directory_entry& copy : std::filesystem::directory_iterator(copies, code))
			std::filesystem::remove(copy.path(), code);

		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, code))
			if (entry.path().extension() == library_extension)
				Open(entry.path());
	};

	void ExtensionLibrary::Open(const std::filesystem::path& library)
	{
		const std::string name = library.filename().string();
		std::error_code code;

		const std::filesystem::path copies = std::filesystem::temp_directory_path(code) / copies_folder;
		std::filesystem::create_directories(copies, code);

		const std::filesystem::path copy = copies / (std::to_string(ProcessId()) + " " + name);
		std::filesystem::copy_file(library, copy, std::filesystem::copy_options::overwrite_existing, code);

		if (code)
		{
			std::cerr << "Could not copy the extension " << name << " (" << code.message() << ")." << std::endl;
			return;
		};

		void* module = OpenModule(copy);

		if (module == nullptr)
		{
			std::cerr << "Could not load the extension " << name << " (" << LastError() << ")." << std::endl;
			std::filesystem::remove(copy, code);
			return;
		};

		const auto abi = (int(*)())Symbol(module, "LoomExtensionAbi");
		const auto start = (void(*)())Symbol(module, "LoomExtensionStart");
		const auto stop = (void(*)())Symbol(module, "LoomExtensionStop");

		const auto reject = [&module, &copy, &code, &name](const std::string& reason)
		{
			std::cerr << "Skipped the extension " << name << ": " << reason << std::endl;
			CloseModule(module);
			std::filesystem::remove(copy, code);
		};

		if (abi == nullptr || start == nullptr || stop == nullptr)
			return reject("it does not export LoomExtensionAbi, LoomExtensionStart and LoomExtensionStop.");

		if (abi() != extension_abi)
			return reject("it was built against another version of the editor.");

		try
		{
			start();
		}
		catch (const std::exception& error)
		{
			return reject(error.what());
		};

		m_loaded.push_back({ module, stop, copy });
	};

	void ExtensionLibrary::Unload()
	{
		for (const Loaded& loaded : m_loaded | std::views::reverse)
			loaded.stop();

		ExtensionRegistry::Panels().clear();

		std::error_code code;

		for (const Loaded& loaded : m_loaded)
		{
			CloseModule(loaded.module);
			std::filesystem::remove(loaded.copy, code);
		};

		m_loaded.clear();
	};
};
