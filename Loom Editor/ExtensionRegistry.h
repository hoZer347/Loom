#pragma once

#include "Loom API.h"

#include "imgui.h"

#include <deque>
#include <functional>
#include <string>


// What the editor looks up in an extension's library. Stop runs before the
// library unloads, so its threads and child processes end there rather than
// under the loader's lock.
#ifdef _WIN32
	#define LOOM_EXTENSION_EXPORT extern "C" __declspec(dllexport)
#else
	#define LOOM_EXTENSION_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace Loom
{
	// An extension calls straight into ImGui and these, and shares the editor's
	// standard library types, so one built against another version or
	// configuration is refused instead of crashing the editor.
#ifdef _DEBUG
	inline constexpr int extension_abi = IMGUI_VERSION_NUM * 100 + 2;
#else
	inline constexpr int extension_abi = IMGUI_VERSION_NUM * 100 + 1;
#endif

	/**
	* Loom::ExtensionRegistry
	* - What an extension adds to the editor. A panel docks, tears off and
	*   shows in the Window menu as the editor's own do, and stays closed
	*   until the user opens it there
	* - An extension is a shared library in the extensions folder, built with
	*   LOOM_SCRIPT_MODULE against the editor's import library as scripts are.
	*   It exports LoomExtensionAbi, LoomExtensionStart and LoomExtensionStop
	*/
	struct LOOM_API ExtensionRegistry final
	{
		struct Panel
		{
			std::string title;
			std::function<void(const std::string& project)> draw;
			bool shown = false;
		};

		// Called between ImGui::Begin and End while the panel shows, given the
		// open project's folder.
		static void AddPanel(
			const std::string& icon,
			const std::string& name,
			std::function<void(const std::string& project)> draw);

		static std::deque<Panel>& Panels();
	};
};
