#pragma once


// The engine is exported from whatever executable links it, so a project's
// script library can import it instead of carrying its own copy. That matters
// more than it sounds: the task queue, the object tables and the component
// registry are all process-wide state, and two copies of them is two engines
// that cannot see each other.
//
// Scripts are compiled with LOOM_SCRIPT_MODULE defined and link against the
// editor's import library.
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
	#ifdef LOOM_SCRIPT_MODULE
		#define LOOM_API __declspec(dllimport)
	#else
		#define LOOM_API __declspec(dllexport)
	#endif
#else
	#define LOOM_API
#endif
