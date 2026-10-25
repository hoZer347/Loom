#pragma once


namespace Loom
{
	// An automated run has nobody to answer a crash dialog, which would come up
	// on the screen of whoever is at the machine and hold the run until they
	// did. abort(), failed asserts and the debug runtime's errors are written
	// to stderr instead, and a crash ends the process without Windows Error
	// Reporting's dialog.
	void ReportCrashesToStderr();
};
