#include "ScriptLibrary.h"

#include "ComponentRegistry.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


namespace Loom
{
	namespace
	{
#ifdef _DEBUG
		const char* const configuration = "Debug";
#else
		const char* const configuration = "Release";
#endif

		// Runs a command line and pipes everything it says to the console, which
		// in the editor means the Console panel.
		bool Run(const std::string& command_line, const char* prefix)
		{
			FILE* pipe = _popen(('"' + command_line + '"').c_str(), "r");

			if (pipe == nullptr)
				return false;

			std::array<char, 512> buffer{ };
			std::string line;

			while (fgets(buffer.data(), (int)buffer.size(), pipe))
			{
				line = buffer.data();

				while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
					line.pop_back();

				if (!line.empty())
					std::cout << prefix << line << std::endl;
			};

			return _pclose(pipe) == 0;
		};

		// When the editor itself was built. A script library older than that was
		// compiled against different engine headers, and the class layouts it
		// baked in may have moved since: the failure mode is not a missing
		// symbol, it is the two modules disagreeing about where a member lives
		// and quietly corrupting the heap.
		std::filesystem::file_time_type EditorBuildTime()
		{
			char buffer[MAX_PATH]{ };

			GetModuleFileNameA(nullptr, buffer, MAX_PATH);

			std::error_code code;

			return std::filesystem::last_write_time(buffer, code);
		};

		// Same, but hands back the output instead of printing it.
		std::string Capture(const std::string& command_line)
		{
			FILE* pipe = _popen(('"' + command_line + '"').c_str(), "r");

			if (pipe == nullptr)
				return "";

			std::array<char, 512> buffer{ };
			std::string output;

			while (fgets(buffer.data(), (int)buffer.size(), pipe))
				output += buffer.data();

			_pclose(pipe);

			return output;
		};

		// Asks vswhere for a file inside an installation. It ships with every
		// installation since 2017 and knows about the ones that are not where
		// you expect, prereleases included. -find prints one line per match, so
		// the first one that is really there wins.
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

			const std::string output = Capture(
				'"' + vswhere.string() + "\" -latest -prerelease -products * " + arguments);

			std::string line;

			for (const char c : output + '\n')
			{
				if (c != '\n' && c != '\r')
				{
					line += c;
					continue;
				};

				if (!line.empty() && std::filesystem::exists(line, code))
					return line;

				line.clear();
			};

			return "";
		};
	};

	// The copies left behind by editors that are no longer running. One still
	// mapped by a live editor refuses to be removed, and its symbols are left
	// alone with it: that editor is still debugging scripts through them.
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

	ScriptLibrary::~ScriptLibrary()
	{
		if (m_worker.joinable())
			m_worker.join();

		// Including the ones nobody wanted: their results go nowhere, but they
		// are still writing MSBuild's output to a console that is about to go.
		for (std::thread& abandoned : m_abandoned)
			if (abandoned.joinable())
				abandoned.join();

		Unload();
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
			"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe",
			"C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\MSBuild\\Current\\Bin\\MSBuild.exe",
			"C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\MSBuild\\Current\\Bin\\MSBuild.exe",
		};

		std::error_code code;

		for (const char* fallback : fallbacks)
			if (std::filesystem::exists(fallback, code))
				return fallback;

		return "";
	};

	const std::string& ScriptLibrary::FindVisualStudio2026()
	{
		// The panel asks every frame and vswhere is a process, so the answer is
		// kept: an installation does not come and go while the editor is up.
		// 2026 is the 18.x line, and it sits beside a 2022 rather than replacing
		// it, so the range narrows -latest to the newest 2026 rather than to the
		// newest of anything.
		static const std::string devenv =
			VsWhere("-version \"[18.0,19.0)\" -find Common7\\IDE\\devenv.exe");

		return devenv;
	};

	void ScriptLibrary::AbandonBuild()
	{
		if (!m_worker.joinable())
			return;

		// Dropped, not waited for: it is blocked in MSBuild and the editor has a
		// frame to draw. The worker holds the only other reference to its result
		// and finishes into that, where nothing reads it.
		m_current.reset();

		m_abandoned.push_back(std::move(m_worker));
	};

	void ScriptLibrary::SetProject(const std::string& project, const std::string& library)
	{
		AbandonBuild();
		Unload();

		m_project = project;
		m_library = library;

		// The cached answer belonged to the project being left behind, and the
		// panel asks often enough that it would still be warm when the new one
		// asks whether it needs building.
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

	bool ScriptLibrary::BuiltAgainstOlderEditor() const
	{
		if (m_library.empty())
			return false;

		std::error_code code;

		if (!std::filesystem::exists(m_library, code))
			return false;

		return std::filesystem::last_write_time(m_library, code) < EditorBuildTime();
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

		// The project compiles whatever headers are there, so adding, removing or
		// renaming one is a change even when no file in it is newer: a copied or
		// renamed file keeps its time, and a deleted one leaves nothing behind.
		// Each of those does touch the folder it was in.
		if (std::filesystem::last_write_time(scripts, code) > built)
			return true;

		for (auto entry = std::filesystem::recursive_directory_iterator(scripts, code);
			entry != std::filesystem::recursive_directory_iterator();
			entry.increment(code))
		{
			if (entry->is_directory(code))
			{
				// Visual Studio's own state, written to all the time the solution
				// is open.
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

		const std::string command =
			'"' + msbuild + "\" \"" + m_project + "\"" +
			(rebuild ? " -t:Rebuild" : "") +
			" -p:Configuration=" + configuration +
			" -p:Platform=x64 -nologo -v:m -m";

		std::cout
			<< "Compiling scripts (" << configuration << ")"
			<< (rebuild ? ", from scratch: the editor has been rebuilt since" : "")
			<< "..." << std::endl;

		m_worker = std::thread(
			[command, build = m_current]()
			{
				build->succeeded = Run(command + " 2>&1", "  ");
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

		// Its worker has run to the end, so this join is immediate. An abandoned
		// one is not here to be joined: it went into m_abandoned with its result,
		// and only the destructor waits for it.
		if (m_worker.joinable())
			m_worker.join();

		m_succeeded = m_current->succeeded;

		m_current.reset();

		// The library has just moved; whatever the cache last decided about it
		// is out of date.
		m_staleCheckedAt = { };

		m_status = m_succeeded
			? "Compiled."
			: "Compile failed.";

		return Progress::Finished;
	};

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

			std::cerr
				<< "Not loading " << m_library
				<< ": it was built against an older editor, and loading it would have"
				   " the two disagree about how objects are laid out." << std::endl;

			return false;
		};

		// Loading a copy leaves the real library free to be overwritten, so a
		// build from Visual Studio does not fail because the editor is running.
		// One per process: a mapped copy is locked, and a shared name would mean
		// the second editor open on a project silently getting no scripts.
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

		// Whatever turns up in the registry while the library loads belongs to
		// it and goes again when it is unloaded: each component type registers
		// itself as the library's statics are initialised. The registry as it
		// stands now is kept alongside: a script may register over a name that
		// already exists, and that factory has to come back rather than stay
		// behind pointing into a library that is no longer mapped.
		const std::map<std::string, ComponentRegistry::Factory> before = ComponentRegistry::All();

		HMODULE module = LoadLibraryA(copy.string().c_str());

		m_types.clear();
		m_before.clear();

		for (const auto& [name, factory] : ComponentRegistry::All())
		{
			const auto previous = before.find(name);

			if (previous == before.end())
				m_types.push_back(name);
			else m_before.emplace(*previous);
		};

		// A library whose initialisation threw is unmapped again, possibly after
		// some of its types got as far as registering.
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
			"Loaded " + std::to_string(m_types.size()) + " component type(s).";

		std::cout << "Scripts: " << m_status << std::endl;

		for (const std::string& type : m_types)
			std::cout << "  " << type << std::endl;

		return true;
	};

	void ScriptLibrary::RestoreRegistry()
	{
		for (const std::string& type : m_types)
			ComponentRegistry::Unregister(type);

		// And put back what was there before, which restores anything the
		// library registered over the top of.
		for (const auto& [name, factory] : m_before)
			ComponentRegistry::Register(name, nullptr, factory);

		m_types.clear();
		m_before.clear();
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
};
