#include "ScriptLibrary.h"

#include "ComponentRegistry.h"
#include "HiddenProcess.h"
#include "LoadProgress.h"
#include "ProjectTemplate.h"
#include "ScriptTests.h"

#include "Utilities/StateReference.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


namespace Loom
{
	namespace
	{
#if defined(LOOM_FINAL)
		const char* const configuration = "Final";
#elif defined(_DEBUG)
		const char* const configuration = "Debug";
#else
		const char* const configuration = "Release";
#endif

		// An older library baked in class layouts that may have moved, which corrupts the heap silently.
		std::filesystem::file_time_type EditorBuildTime()
		{
			char buffer[MAX_PATH]{ };

			GetModuleFileNameA(nullptr, buffer, MAX_PATH);

			std::error_code code;

			return std::filesystem::last_write_time(buffer, code);
		};

		// vswhere ships with every Visual Studio since 2017; the first match that exists wins.
		std::string VsWhere(const std::string& arguments)
		{
			char program_files[MAX_PATH]{ };

			if (!GetEnvironmentVariableA("ProgramFiles(x86)", program_files, MAX_PATH))
				snprintf(program_files, sizeof(program_files), "C:\\Program Files (x86)");

			const std::filesystem::path vswhere =
				std::filesystem::path(program_files) /
				"Microsoft Visual Studio" / "Installer" / "vswhere.exe";

			std::error_code code;

			if (!std::filesystem::exists(vswhere, code))
				return "";

			std::string found;

			HiddenProcess::Run(
				'"' + vswhere.string() + "\" -latest -prerelease -products * " + arguments,
				[&](const std::string& line)
				{
					if (found.empty() && !line.empty() && std::filesystem::exists(line, code))
						found = line;
				});

			return found;
		};
	};

	#pragma region Lifetime

	ScriptLibrary::~ScriptLibrary()
	{
		if (m_worker.joinable())
			m_worker.join();

		// Abandoned workers still write MSBuild output to a log about to go.
		for (std::thread& abandoned : m_abandoned)
			if (abandoned.joinable())
				abandoned.join();

		Unload();
	};

	#pragma endregion

	#pragma region Project

	void ScriptLibrary::SetProject(const std::string& project, const std::string& library)
	{
		AbandonBuild();
		Unload();

		m_project = project;
		m_library = library;

		// The cached answer belonged to the project being left.
		m_staleCheckedAt = { };

		std::error_code code;

		m_status = std::filesystem::exists(m_library, code)
			? "Built, not loaded."
			: "Not built yet.";
	};

	void ScriptLibrary::Clear()
	{
		AbandonBuild();
		Unload();

		m_project.clear();
		m_library.clear();
		m_staleCheckedAt = { };
		m_status = "No script project.";
	};

	bool ScriptLibrary::IsStale() const
	{
		std::error_code code;

		if (m_project.empty())
			return false;

		const auto now = std::chrono::steady_clock::now();

		if (now - m_staleCheckedAt < std::chrono::milliseconds(500))
			return m_stale;

		m_staleCheckedAt = now;
		m_stale = true;

		if (!std::filesystem::exists(m_library, code))
			return true;

		const auto built = std::filesystem::last_write_time(m_library, code);

		if (BuiltAgainstOlderEditor())
			return true;

		const std::filesystem::path scripts = std::filesystem::path(m_project).parent_path();

		// Adding, removing or renaming a header touches its folder but not the file times.
		if (std::filesystem::last_write_time(scripts, code) > built)
			return true;

		for (auto entry = std::filesystem::recursive_directory_iterator(scripts, code);
			entry != std::filesystem::recursive_directory_iterator();
			entry.increment(code))
		{
			if (entry->is_directory(code))
			{
				// Visual Studio's own state, written all the time the solution is open.
				if (entry->path().filename().string().front() == '.')
					entry.disable_recursion_pending();
				else if (entry->last_write_time(code) > built)
					return true;

				continue;
			};

			if (!entry->is_regular_file(code))
				continue;

			const std::filesystem::path extension = entry->path().extension();

			if (extension != ".hpp" && extension != ".cpp" && extension != ".h" && extension != ".vcxproj")
				continue;

			if (entry->last_write_time(code) > built)
				return true;
		};

		m_stale = false;

		return false;
	};

	#pragma endregion

	#pragma region Building

	bool ScriptLibrary::Build()
	{
		if (m_project.empty() || IsBuilding())
			return false;

		const std::string msbuild = FindMSBuild();

		if (msbuild.empty())
		{
			m_status = "No MSBuild found.";
			std::cerr
				<< "Cannot compile scripts: no Visual Studio build tools found."
				<< std::endl;

			return false;
		};

		// A worker from a build that already finished; joining it is immediate.
		if (m_worker.joinable())
			m_worker.join();

		m_current = std::make_shared<Compilation>();
		m_succeeded = false;
		m_status = "Compiling...";

		const bool rebuild = BuiltAgainstOlderEditor();

		m_rebuilding = rebuild;
		m_buildStarted = std::chrono::steady_clock::now();
		m_expected.reset();

		if (!m_library.empty())
		{
			std::ifstream in(TimingsFile());
			m_expected = BuildTimings::Read(in).Of(rebuild);
		};

		std::error_code code;
		m_libraryBefore = std::filesystem::last_write_time(m_library, code);

		const std::string command =
			'"' + msbuild + "\" \"" + m_project + "\"" +
			(rebuild ? " -t:Rebuild" : "") +
			" -p:Configuration=" + configuration +
			" -p:Platform=x64 -nologo -v:m -m \"-clp:ErrorsOnly;WarningsOnly\"";

		m_worker = std::thread(
			[command, build = m_current]()
			{
				build->succeeded = HiddenProcess::Run(
					command,
					[](const std::string& line)
					{
						if (const std::string shown = ProjectTemplate::BuildLine(line); !shown.empty())
							std::cout << "  " << shown << std::endl;
					});
				build->done = true;
			});

		return true;
	};

	ScriptLibrary::Progress ScriptLibrary::Poll()
	{
		if (!m_current)
			return Progress::Idle;

		if (!m_current->done)
			return Progress::Building;

		// Its worker has finished, so this join is immediate.
		if (m_worker.joinable())
			m_worker.join();

		m_succeeded = m_current->succeeded;

		m_current.reset();

		// Only a build that wrote the library predicts the next one's time.
		std::error_code code;

		if (m_succeeded && !m_library.empty() &&
			std::filesystem::last_write_time(m_library, code) != m_libraryBefore)
		{
			const std::filesystem::path file = TimingsFile();

			std::ifstream in(file);
			BuildTimings timings = BuildTimings::Read(in);
			in.close();

			timings.Set(m_rebuilding,
				std::chrono::duration<double>(std::chrono::steady_clock::now() - m_buildStarted).count());

			std::ofstream out(file);
			timings.Write(out);
		};

		// The library has just moved, so the cache is out of date.
		m_staleCheckedAt = { };

		m_status = m_succeeded
			? "Compiled."
			: "Compile failed.";

		return Progress::Finished;
	};

	std::optional<float> ScriptLibrary::BuildProgress() const
	{
		if (IsIdle())
			return std::nullopt;

		return BuildTimings::Progress(
			std::chrono::duration<double>(std::chrono::steady_clock::now() - m_buildStarted).count(),
			m_expected);
	};

	void ScriptLibrary::AbandonBuild()
	{
		if (!m_worker.joinable())
			return;

		// Dropped, not waited for, since it is blocked in MSBuild and a frame needs drawing.
		m_current.reset();

		m_abandoned.push_back(std::move(m_worker));
	};

	bool ScriptLibrary::BuiltAgainstOlderEditor() const
	{
		if (m_library.empty())
			return false;

		std::error_code code;

		if (!std::filesystem::exists(m_library, code))
			return false;

		return std::filesystem::last_write_time(m_library, code) < EditorBuildTime();
	};

	std::filesystem::path ScriptLibrary::TimingsFile() const
	{
		return std::filesystem::path(m_library).replace_extension(BuildTimings::Extension);
	};

	#pragma endregion

	#pragma region Loading

	bool ScriptLibrary::Load()
	{
		if (m_module || m_library.empty())
			return false;

		std::error_code code;

		if (!std::filesystem::exists(m_library, code))
		{
			m_status = "Not built yet.";
			return false;
		};

		if (BuiltAgainstOlderEditor())
		{
			m_status = "Built against an older editor; needs recompiling.";
			return false;
		};

		// A loaded copy frees the real library for Visual Studio to rebuild; one per process, since a mapped copy is locked.
		SweepLoadedCopies(m_library);

		std::filesystem::path copy(m_library);
		copy.replace_extension("");
		copy += ".loaded." + std::to_string(GetCurrentProcessId()) + ".dll";

		std::filesystem::copy_file(
			m_library,
			copy,
			std::filesystem::copy_options::overwrite_existing,
			code);

		if (code)
		{
			m_status = "Could not copy the library: " + code.message();
			std::cerr << m_status << std::endl;
			return false;
		};

		// The debugger wants the symbols beside the module it actually loaded.
		std::filesystem::path symbols(m_library);
		symbols.replace_extension(".pdb");

		if (std::filesystem::exists(symbols, code))
		{
			std::filesystem::path symbols_copy(copy);
			symbols_copy.replace_extension(".pdb");

			std::filesystem::copy_file(
				symbols,
				symbols_copy,
				std::filesystem::copy_options::overwrite_existing,
				code);
		};

		// Types register as the library loads; the previous registry is kept to restore names it overwrites.
		std::map<std::string, std::pair<ComponentRegistry::Factory, std::string>> before{ };

		for (const auto& [name, factory] : ComponentRegistry::All())
			before.emplace(name, std::make_pair(factory, ComponentRegistry::CategoryOf(name)));
		const std::vector<std::string> states_before = StateRegistry::Names();
		m_testsBefore = ScriptTests::All().size();

		HMODULE module = LoadLibraryA(copy.string().c_str());

		m_types.clear();
		m_before.clear();
		m_states.clear();

		std::ranges::set_difference(StateRegistry::Names(), states_before, std::back_inserter(m_states));

		for (const auto& [name, factory] : ComponentRegistry::All())
		{
			const auto previous = before.find(name);

			if (previous == before.end())
				m_types.push_back(name);
			else m_before.emplace(name, previous->second);
		};

		// A library whose initialisation threw may have registered some types already.
		if (module == nullptr)
		{
			const DWORD error = GetLastError();

			RestoreRegistry();

			m_status = "Could not load the library (error " + std::to_string(error) + ").";
			std::cerr << m_status << std::endl;
			return false;
		};

		m_module = module;
		m_loadedCopy = copy.string();

		m_status =
			"Loaded " + std::to_string(m_types.size()) + " component type(s), " +
			std::to_string(m_states.size()) + " state(s) and " +
			std::to_string(ScriptTests::All().size() - m_testsBefore) + " test(s).";

		return true;
	};

	void ScriptLibrary::Unload()
	{
		if (m_module == nullptr)
			return;

		RestoreRegistry();

		FreeLibrary((HMODULE)m_module);
		m_module = nullptr;

		std::error_code code;

		std::filesystem::remove(m_loadedCopy, code);

		// The symbols went with it, and a .pdb nothing points at is litter.
		std::filesystem::remove(
			std::filesystem::path(m_loadedCopy).replace_extension(".pdb"), code);

		m_loadedCopy.clear();

		m_status = "Unloaded.";
	};

	// A copy still mapped by a live editor is left, symbols and all.
	void ScriptLibrary::SweepLoadedCopies(const std::string& library)
	{
		const std::filesystem::path path(library);
		const std::string prefix = path.stem().string() + ".loaded.";

		std::error_code code;

		for (const auto& entry :
			std::filesystem::directory_iterator(path.parent_path(), code))
		{
			const std::filesystem::path& copy = entry.path();

			if (copy.extension() != ".dll" ||
				copy.filename().string().rfind(prefix, 0) != 0)
				continue;

			std::error_code removing;

			if (std::filesystem::remove(copy, removing) && !removing)
				std::filesystem::remove(
					std::filesystem::path(copy).replace_extension(".pdb"), code);
		};
	};

	void ScriptLibrary::RestoreRegistry()
	{
		for (const std::string& type : m_types)
			ComponentRegistry::Unregister(type);

		for (const std::string& state : m_states)
			StateRegistry::Unregister(state);

		m_states.clear();

		ScriptTests::Truncate(m_testsBefore);

		// Restores anything the library registered over.
		for (const auto& [name, entry] : m_before)
			ComponentRegistry::Register(name, nullptr, entry.first, entry.second);

		m_types.clear();
		m_before.clear();
	};

	#pragma endregion

	#pragma region Tools

	const std::string& ScriptLibrary::FindVisualStudio()
	{
		// vswhere is a process, and installations do not change while the editor is up.
		static const std::string devenv = VsWhere("-find Common7\\IDE\\devenv.exe");

		return devenv;
	};

	std::string ScriptLibrary::FindMSBuild()
	{
		const std::string found = VsWhere(
			"-requires Microsoft.Component.MSBuild "
			"-find MSBuild\\**\\Bin\\MSBuild.exe");

		if (!found.empty())
			return found;

		const char* const fallbacks[] =
		{
			"C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe",
			"C:\\Program Files\\Microsoft Visual Studio\\18\\Professional\\MSBuild\\Current\\Bin\\MSBuild.exe",
			"C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\MSBuild\\Current\\Bin\\MSBuild.exe",
		};

		std::error_code code;

		for (const char* fallback : fallbacks)
			if (std::filesystem::exists(fallback, code))
				return fallback;

		return "";
	};

	const char* ScriptLibrary::Configuration()
	{
		return configuration;
	};

	#pragma endregion
};
