#include "WebPlayer.h"

#include <filesystem>
#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#endif


namespace Loom
{
#ifdef _WIN32
	namespace
	{
		const char* const prefix = "[web] ";

		void OpenInBrowser(const std::string& url)
		{
			// ShellExecute may hand the work to a shell extension, which wants COM.
			const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

			const HINSTANCE result = ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

			// The one error code ShellExecute has: anything at or below 32.
			if ((INT_PTR)result <= 32)
				std::cerr << prefix << "Could not open " << url << " (error " << (INT_PTR)result << ')' << std::endl;

			if (SUCCEEDED(com))
				CoUninitialize();
		};

		// Returns when everything holding the pipe has exited.
		void Read(HiddenProcess hidden, const std::string& page, const std::atomic<bool>& stopping)
		{
			WebPlayer::Output output(page);

			hidden.ReadLines(
				[&](const std::string& line)
				{
					if (const std::string url = output.Hear(line); !url.empty())
						OpenInBrowser(url);
				});

			hidden.Finish();

			for (const std::string& line : output.Ended(stopping))
				std::cout << prefix << line << std::endl;
		};
	};

	bool WebPlayer::Launch(const std::string& project, const std::string& scene)
	{
		// Where play-web.ps1 writes the page: beside the scene, served from the project.
		const std::string page =
			'/' + (std::filesystem::path(scene).parent_path().lexically_relative(project) / "index.html").generic_string();

		const bool started = m_run.Start("play-web.ps1",
			"-Project " + ScriptRun::Quote(project) + " -Scene " + ScriptRun::Quote(scene),
			[page](HiddenProcess hidden, const std::atomic<bool>& stopping)
			{
				Read(hidden, page, stopping);
			});

		if (started)
			std::cout << prefix << "Building " << project << " for the web" << std::endl;

		return started;
	};
#else
	bool WebPlayer::Launch(const std::string&, const std::string&)
	{
		return false;
	};
#endif
};
