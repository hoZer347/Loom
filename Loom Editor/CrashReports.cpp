#include "CrashReports.h"

// The editor only runs on Windows, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include <crtdbg.h>
#include <cstdlib>
#include <initializer_list>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


namespace Loom
{
	void ReportCrashesToStderr()
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		_set_error_mode(_OUT_TO_STDERR);
		_set_abort_behavior(0, _CALL_REPORTFAULT);

		for (const int report : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
		{
			_CrtSetReportMode(report, _CRTDBG_MODE_FILE);
			_CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
		};
	};
};

#endif
