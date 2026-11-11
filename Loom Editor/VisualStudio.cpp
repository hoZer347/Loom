#include "VisualStudio.h"

// The editor runs on Windows and Linux, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "HiddenProcess.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <span>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <oleauto.h>
#include <wrl/client.h>
#else
#include <cstdlib>
#endif


namespace Loom
{
#ifdef _WIN32

	namespace
	{
		#pragma region Automation

		using Microsoft::WRL::ComPtr;

		// IDispatch takes the arguments last first.
		HRESULT Call(
			IDispatch* object,
			const wchar_t* member,
			WORD kind,
			VARIANT* result,
			std::span<VARIANT> arguments = { })
		{
			DISPID id = 0;
			LPOLESTR name = const_cast<LPOLESTR>(member);

			const HRESULT found = object->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &id);

			if (FAILED(found))
				return found;

			DISPPARAMS parameters{ arguments.data(), nullptr, (UINT)arguments.size(), 0 };

			return object->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, kind, &parameters, result, nullptr, nullptr);
		};

		HRESULT Property(IDispatch* object, const wchar_t* member, ComPtr<IDispatch>& result)
		{
			VARIANT value;
			VariantInit(&value);

			const HRESULT called = Call(object, member, DISPATCH_PROPERTYGET, &value);

			if (FAILED(called) || value.vt != VT_DISPATCH)
			{
				VariantClear(&value);
				result.Reset();

				return FAILED(called) ? called : E_NOINTERFACE;
			};

			result.Attach(value.pdispVal);

			return S_OK;
		};

		VARIANT Integer(int value)
		{
			VARIANT integer;
			VariantInit(&integer);
			integer.vt = VT_I4;
			integer.lVal = value;

			return integer;
		};

		// The first column when none is given.
		HRESULT GoTo(IDispatch* window, int line, int column)
		{
			ComPtr<IDispatch> document;
			ComPtr<IDispatch> selection;

			if (const HRESULT found = Property(window, L"Document", document); FAILED(found))
				return found;

			if (const HRESULT found = Property(document.Get(), L"Selection", selection); FAILED(found))
				return found;

			VARIANT position[]{ Integer((std::max)(column, 1)), Integer(line) };

			return Call(selection.Get(), L"MoveToLineAndOffset", DISPATCH_METHOD, nullptr, position);
		};

		// What a Visual Studio in the middle of a build or a modal dialog answers.
		bool IsBusy(HRESULT result)
		{
			return result == RPC_E_CALL_REJECTED || result == RPC_E_SERVERCALL_RETRYLATER;
		};

		// Each Visual Studio registers as "!VisualStudio.DTE.<version>:<pid>"; busy when one will not answer.
		ComPtr<IDispatch> FindVisualStudioWith(const std::string& solution, DWORD& process, bool& busy)
		{
			ComPtr<IRunningObjectTable> table;
			ComPtr<IEnumMoniker> monikers;
			ComPtr<IBindCtx> context;

			if (FAILED(GetRunningObjectTable(0, &table)) ||
				FAILED(table->EnumRunning(&monikers)) ||
				FAILED(CreateBindCtx(0, &context)))
				return nullptr;

			const std::wstring prefix = L"!VisualStudio.DTE.";

			ComPtr<IMoniker> moniker;

			while (monikers->Next(1, moniker.ReleaseAndGetAddressOf(), nullptr) == S_OK)
			{
				LPOLESTR name = nullptr;

				if (FAILED(moniker->GetDisplayName(context.Get(), nullptr, &name)))
					continue;

				const std::wstring display = name;
				CoTaskMemFree(name);

				ComPtr<IUnknown> object;
				ComPtr<IDispatch> dte;

				if (display.rfind(prefix, 0) != 0 ||
					FAILED(table->GetObject(moniker.Get(), &object)) ||
					FAILED(object.As(&dte)))
					continue;

				VARIANT open;
				VARIANT path;
				VariantInit(&open);
				VariantInit(&path);

				HRESULT result = Call(dte.Get(), L"Solution", DISPATCH_PROPERTYGET, &open);

				if (SUCCEEDED(result) && open.vt == VT_DISPATCH)
					result = Call(open.pdispVal, L"FullName", DISPATCH_PROPERTYGET, &path);

				busy = busy || IsBusy(result);

				std::error_code code;

				const bool match =
					SUCCEEDED(result) &&
					path.vt == VT_BSTR &&
					SysStringLen(path.bstrVal) > 0 &&
					std::filesystem::equivalent(std::filesystem::path(path.bstrVal), solution, code);

				VariantClear(&path);
				VariantClear(&open);

				if (match)
				{
					process = wcstoul(display.substr(display.rfind(L':') + 1).c_str(), nullptr, 10);
					return dte;
				};
			};

			return nullptr;
		};

		// How long a busy Visual Studio is asked again before its refusal is believed.
		constexpr DWORD busyRetryTimeout = 5000;
		constexpr DWORD busyRetryDelay = 100;
		constexpr DWORD noRetry = (DWORD)-1;

		// Retries calls a busy Visual Studio rejects; lives on the stack, so it counts no references.
		struct RetryBusyCalls final : IMessageFilter
		{
			RetryBusyCalls()
			{
				m_registered = SUCCEEDED(CoRegisterMessageFilter(this, &m_previous));
			};

			~RetryBusyCalls()
			{
				if (m_registered)
				{
					IMessageFilter* ours = nullptr;
					CoRegisterMessageFilter(m_previous, &ours);

					if (m_previous)
						m_previous->Release();
				};
			};

			RetryBusyCalls(const RetryBusyCalls&) = delete;
			RetryBusyCalls& operator=(const RetryBusyCalls&) = delete;

			STDMETHODIMP QueryInterface(REFIID id, void** object) override
			{
				if (id != IID_IUnknown && id != IID_IMessageFilter)
				{
					*object = nullptr;
					return E_NOINTERFACE;
				};

				*object = static_cast<IMessageFilter*>(this);
				return S_OK;
			};

			STDMETHODIMP_(ULONG) AddRef() override { return 1; };
			STDMETHODIMP_(ULONG) Release() override { return 1; };

			STDMETHODIMP_(DWORD) HandleInComingCall(DWORD, HTASK, DWORD, LPINTERFACEINFO) override
			{
				return SERVERCALL_ISHANDLED;
			};

			STDMETHODIMP_(DWORD) RetryRejectedCall(HTASK, DWORD elapsed, DWORD) override
			{
				return elapsed < busyRetryTimeout ? busyRetryDelay : noRetry;
			};

			STDMETHODIMP_(DWORD) MessagePending(HTASK, DWORD, DWORD) override
			{
				return PENDINGMSG_WAITDEFPROCESS;
			};

		private:
			IMessageFilter* m_previous = nullptr;
			bool m_registered = false;
		};

		enum struct Handover
		{
			Opened,
			NotRunning,

			Busy,

			Failed,
		};

		// A line of zero leaves the caret where Visual Studio puts it.
		Handover OpenThroughAutomation(const std::string& solution, const std::string& file, int line, int column)
		{
			DWORD process = 0;
			bool busy = false;

			const ComPtr<IDispatch> dte = FindVisualStudioWith(solution, process, busy);

			if (!dte)
				return busy ? Handover::Busy : Handover::NotRunning;

			ComPtr<IDispatch> operations;
			ComPtr<IDispatch> window;

			const HRESULT reached = Property(dte.Get(), L"ItemOperations", operations);
			Property(dte.Get(), L"MainWindow", window);

			VARIANT path;
			VariantInit(&path);
			path.vt = VT_BSTR;
			path.bstrVal = SysAllocString(std::filesystem::path(file).wstring().c_str());

			VARIANT opened;
			VariantInit(&opened);

			const HRESULT result = SUCCEEDED(reached)
				? Call(operations.Get(), L"OpenFile", DISPATCH_METHOD, &opened, { &path, 1 })
				: reached;

			const HRESULT moved = SUCCEEDED(result) && line > 0 && opened.vt == VT_DISPATCH
				? GoTo(opened.pdispVal, line, column)
				: S_OK;

			VariantClear(&opened);
			VariantClear(&path);

			if (IsBusy(result))
				return Handover::Busy;

			if (FAILED(result))
			{
				std::cerr
					<< "Visual Studio has " << solution << " open but would not open "
					<< file << " (error 0x" << std::hex << result << std::dec << ')' << std::endl;

				return Handover::Failed;
			};

			if (FAILED(moved))
				std::cerr
					<< "Visual Studio opened " << file << " but would not go to line " << line
					<< " (error 0x" << std::hex << moved << std::dec << ')' << std::endl;

			// Windows only lets another process take the foreground when the holder allows it.
			AllowSetForegroundWindow(process);

			if (window)
				Call(window.Get(), L"Activate", DISPATCH_METHOD, nullptr);

			return Handover::Opened;
		};

		// The editor's thread does not keep COM up, so the call brings it up.
		bool OpenInRunningVisualStudio(const std::string& solution, const std::string& file, int line, int column)
		{
			const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

			Handover handover;

			{
				const RetryBusyCalls retry;
				handover = OpenThroughAutomation(solution, file, line, column);
			};

			// A busy one may have the solution, and starting another would open it twice.
			if (handover == Handover::Busy)
				std::cerr << "Visual Studio is busy; try again once it has finished." << std::endl;

			if (SUCCEEDED(com))
				CoUninitialize();

			return handover != Handover::NotRunning;
		};

		// Long enough for a cold start of Visual Studio on a large solution.
		constexpr std::chrono::seconds visualStudioStartTimeout{ 180 };
		constexpr std::chrono::milliseconds visualStudioPollInterval{ 500 };

		// A fresh Visual Studio answers nothing until loaded, and /Command File.OpenFile fails with dialogs.
		std::thread OpenOnceLoaded(const std::string& solution, const std::string& file, int line, int column)
		{
			return std::thread(
				[solution, file, line, column]()
				{
					const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

					const auto deadline = std::chrono::steady_clock::now() + visualStudioStartTimeout;

					Handover handover = Handover::NotRunning;

					{
						const RetryBusyCalls retry;

						while (std::chrono::steady_clock::now() < deadline)
						{
							handover = OpenThroughAutomation(solution, file, line, column);

							if (handover == Handover::Opened || handover == Handover::Failed)
								break;

							std::this_thread::sleep_for(visualStudioPollInterval);
						};
					};

					if (handover == Handover::NotRunning || handover == Handover::Busy)
						std::cerr << "Visual Studio did not load " << solution << " in time to open " << file << std::endl;

					if (SUCCEEDED(com))
						CoUninitialize();
				});
		};

		#pragma endregion
	};

	#pragma region Shell

	void VisualStudio::Shell(const char* file, const std::string& parameters)
	{
		const HINSTANCE result = ShellExecuteA(
			nullptr,
			"open",
			file,
			parameters.empty() ? nullptr : parameters.c_str(),
			nullptr,
			SW_SHOWNORMAL);

		// The one error code ShellExecute has: anything at or below 32.
		if ((INT_PTR)result <= 32)
			std::cerr
				<< "Could not open " << file << ' ' << parameters
				<< " (error " << (INT_PTR)result << ')' << std::endl;
	};

	#pragma endregion

	#pragma region Installation

	std::string VisualStudio::Where(const std::string& arguments)
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
			[&code, &found](const std::string& line)
			{
				if (found.empty() && !line.empty() && std::filesystem::exists(line, code))
					found = line;
			});

		return found;
	};

	const std::string& VisualStudio::Find()
	{
		// vswhere is a process, and installations do not change while the editor is up.
		static const std::string devenv = Where("-find Common7\\IDE\\devenv.exe");

		return devenv;
	};

	#pragma endregion

#else

	namespace
	{
		// Quoted for /bin/sh, where nothing inside single quotes is special but the quote itself.
		std::string Quote(const std::string& argument)
		{
			std::string quoted = "'";

			for (const char c : argument)
				quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);

			return quoted + '\'';
		};

		// Started and left running, its output dropped.
		void Detach(const std::string& command)
		{
			if (std::system((command + " >/dev/null 2>&1 &").c_str()) != 0)
				std::cerr << "Could not run " << command << std::endl;
		};
	};

	std::string VisualStudio::OnPath(const char* program)
	{
		const char* const path = std::getenv("PATH");
		std::error_code code;

		for (std::string_view rest = path != nullptr ? path : ""; !rest.empty(); )
		{
			const size_t end = rest.find(':');
			const std::filesystem::path candidate = std::filesystem::path(rest.substr(0, end)) / program;

			if (std::filesystem::is_regular_file(candidate, code))
				return candidate.string();

			rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
		};

		return "";
	};

	#pragma region Shell

	void VisualStudio::Shell(const char* file, const std::string& parameters)
	{
		Detach(parameters.empty() ? "xdg-open " + Quote(file) : Quote(file) + ' ' + parameters);
	};

	#pragma endregion

	#pragma region Installation

	// Visual Studio has no Linux build.
	std::string VisualStudio::Where(const std::string&)
	{
		return "";
	};

	const std::string& VisualStudio::Find()
	{
		static const std::string none;

		return none;
	};

	#pragma endregion

#endif

	#pragma region Opening

	bool VisualStudio::IsHeader(const std::filesystem::path& file)
	{
		return std::ranges::equal(file.extension().string(), std::string_view(header_extension),
			[](char a, char b) { return tolower((unsigned char)a) == tolower((unsigned char)b); });
	};

	std::filesystem::path VisualStudio::Normalise(const std::filesystem::path& path)
	{
		std::error_code code;

		return std::filesystem::absolute(path, code).lexically_normal();
	};

	std::filesystem::path VisualStudio::Reroot(const std::filesystem::path& file, const std::filesystem::path& root)
	{
		std::error_code code;

		const std::filesystem::path normalised = Normalise(file);

		if (std::filesystem::exists(normalised, code) || root.empty())
			return normalised;

		const std::filesystem::path normalisedRoot = Normalise(root);

		std::vector<std::filesystem::path> parts;

		for (const std::filesystem::path& part : normalised.relative_path())
			parts.push_back(part);

		// The longest tail first, so Scripts/Grid.hpp wins over a bare Grid.hpp.
		for (size_t first = 0; first < parts.size(); ++first)
		{
			std::filesystem::path candidate = normalisedRoot;

			for (size_t part = first; part < parts.size(); ++part)
				candidate /= parts[part];

			if (std::filesystem::exists(candidate, code))
				return candidate;
		};

		return normalised;
	};

	std::string VisualStudio::SolutionFor(const std::filesystem::path& file)
	{
		std::error_code code;

		for (std::filesystem::path folder = std::filesystem::absolute(file, code).parent_path(); ; folder = folder.parent_path())
		{
			std::vector<std::filesystem::path> solutions;

			for (const auto& entry : std::filesystem::directory_iterator(folder, code))
				if (entry.path().extension() == ".sln")
					solutions.push_back(entry.path());

			if (!solutions.empty())
				return (std::ranges::min)(solutions).string();

			if (folder == folder.parent_path())
				return "";
		};
	};

	std::thread VisualStudio::Open(const std::string& solutionPath, const std::string& filePath, int line, int column)
	{
		std::error_code missing;

		const std::string file = Normalise(filePath).string();
		const std::string solution = solutionPath.empty() ? "" : Normalise(solutionPath).string();

		if (!std::filesystem::exists(file, missing))
		{
			std::cerr << file << " does not exist" << std::endl;
			return { };
		};

#ifndef _WIN32
		// Visual Studio Code where it is installed, which takes a line and column too.
		static const std::string code_editor = OnPath("code");

		if (code_editor.empty())
			Shell(file.c_str(), "");
		else Detach(Quote(code_editor) + " --goto " + Quote(file + ':' + std::to_string((std::max)(line, 1)) + ':' + std::to_string((std::max)(column, 1))));

		return { };
#else
		std::error_code code;
		const bool has_solution = !solution.empty() && std::filesystem::exists(solution, code);

		if (has_solution && OpenInRunningVisualStudio(solution, file, line, column))
			return { };

		const std::string& devenv = Find();

		if (!devenv.empty() && has_solution)
		{
			Shell(devenv.c_str(), '"' + solution + '"');
			return OpenOnceLoaded(solution, file, line, column);
		};

		if (!devenv.empty())
			Shell(devenv.c_str(), "/Edit \"" + file + '"');
		else if (has_solution)
			Shell(solution.c_str(), "");
		else if (IsHeader(file))
			std::cerr << "No Visual Studio to open " << file << " in" << std::endl;
		else
			Shell(file.c_str(), "");

		return { };
#endif
	};

	#pragma endregion
};

#endif
