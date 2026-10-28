#include "VisualStudio.h"

// The editor only runs on Windows, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "HiddenProcess.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <span>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <oleauto.h>
#include <wrl/client.h>


namespace Loom
{
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

		ComPtr<IDispatch> Property(IDispatch* object, const wchar_t* member)
		{
			VARIANT value;
			VariantInit(&value);

			if (FAILED(Call(object, member, DISPATCH_PROPERTYGET, &value)) || value.vt != VT_DISPATCH)
			{
				VariantClear(&value);
				return nullptr;
			};

			ComPtr<IDispatch> result;
			result.Attach(value.pdispVal);

			return result;
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
			const ComPtr<IDispatch> document = Property(window, L"Document");
			const ComPtr<IDispatch> selection = document ? Property(document.Get(), L"Selection") : nullptr;

			if (!selection)
				return E_NOINTERFACE;

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

			const ComPtr<IDispatch> operations = Property(dte.Get(), L"ItemOperations");
			const ComPtr<IDispatch> window = Property(dte.Get(), L"MainWindow");

			VARIANT path;
			VariantInit(&path);
			path.vt = VT_BSTR;
			path.bstrVal = SysAllocString(std::filesystem::path(file).wstring().c_str());

			VARIANT opened;
			VariantInit(&opened);

			const HRESULT result = operations
				? Call(operations.Get(), L"OpenFile", DISPATCH_METHOD, &opened, { &path, 1 })
				: E_NOINTERFACE;

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

			const Handover handover = OpenThroughAutomation(solution, file, line, column);

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

					while (std::chrono::steady_clock::now() < deadline)
					{
						handover = OpenThroughAutomation(solution, file, line, column);

						if (handover == Handover::Opened || handover == Handover::Failed)
							break;

						std::this_thread::sleep_for(visualStudioPollInterval);
					};

					if (handover == Handover::NotRunning || handover == Handover::Busy)
						std::cerr << "Visual Studio did not load " << solution << " in time to open " << file << std::endl;

					if (SUCCEEDED(com))
						CoUninitialize();
				});
		};

		#pragma endregion

		#pragma region Starting

		void Start(const std::string& file, const std::string& parameters)
		{
			const HINSTANCE result = ShellExecuteA(
				nullptr,
				"open",
				file.c_str(),
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
	};

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
			[&](const std::string& line)
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

	#pragma region Opening

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

	std::thread VisualStudio::Open(const std::string& solution, const std::string& file, int line, int column)
	{
		std::error_code code;
		const bool has_solution = !solution.empty() && std::filesystem::exists(solution, code);

		if (has_solution && OpenInRunningVisualStudio(solution, file, line, column))
			return { };

		const std::string& devenv = Find();

		if (!devenv.empty() && has_solution)
		{
			Start(devenv, '"' + solution + '"');
			return OpenOnceLoaded(solution, file, line, column);
		};

		if (!devenv.empty())
			Start(devenv, "/Edit \"" + file + '"');
		else if (has_solution)
			Start(solution, "");
		else
			std::cerr << "No Visual Studio to open " << file << " in" << std::endl;

		return { };
	};

	#pragma endregion
};

#endif
