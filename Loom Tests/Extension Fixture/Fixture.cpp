#include "imgui.h"

#include <functional>
#include <string>

// Built with LOOM_FIXTURE_OLD_API it is what an extension compiled before
// revision 2 of the API has inside: that AddPanel, and that revision in its ABI.
#ifdef LOOM_FIXTURE_OLD_API
	#define LOOM_EXTENSION_EXPORT extern "C" __declspec(dllexport)

	namespace Loom
	{
		inline constexpr int ExtensionApiRevision = 1;
		inline constexpr int ExtensionAbi = (IMGUI_VERSION_NUM * 100 + ExtensionApiRevision) * 10 + 1;

		struct __declspec(dllimport) ExtensionRegistry final
		{
			static void AddPanel(
				const std::string& icon,
				const std::string& name,
				std::function<void(const std::string& project)> draw,
				std::function<void(const std::string& project)> update = { });
		};
	};
#else
	#include "ExtensionRegistry.h"
#endif


// Built with LOOM_FIXTURE_PROJECT it is a second extension with names of its own,
// for a project to load beside the one in the extensions folder.
#ifdef LOOM_FIXTURE_PROJECT
	#define LOOM_FIXTURE_NAME "Project Fixture"
	#define LOOM_FIXTURE_PREFIX "Project"
#else
	#define LOOM_FIXTURE_NAME "Reload Fixture"
	#define LOOM_FIXTURE_PREFIX "Fixture"
#endif

namespace
{
	constexpr const char* panel_icon = "F";
	constexpr const char* panel_name = LOOM_FIXTURE_NAME;
	constexpr const char* waiting_label = LOOM_FIXTURE_PREFIX " Waiting##Fixture";
	constexpr const char* clicked_label = LOOM_FIXTURE_PREFIX " Clicked##Fixture";
	constexpr const char* file_used_label = LOOM_FIXTURE_PREFIX " File Item Used##Fixture";
	constexpr const char* custom_used_label = LOOM_FIXTURE_PREFIX " Custom Item Used##Fixture";
	constexpr const char* file_menu = "File";
	constexpr const char* file_item = LOOM_FIXTURE_PREFIX " File Item";
	constexpr const char* custom_menu = LOOM_FIXTURE_PREFIX " Menu";
	constexpr const char* custom_item = LOOM_FIXTURE_PREFIX " Custom Item";
	constexpr const char* late_panel_name = LOOM_FIXTURE_PREFIX " Late Panel";
	constexpr const char* editor_menus[] = { "Edit", "GameObject", "Window" };
	constexpr const char* editor_menu_item = LOOM_FIXTURE_PREFIX " Editor Menu Item";

	bool clicked = false;
	bool file_used = false;
	bool custom_used = false;
	bool late_added = false;

	void AddLatePanel()
	{
		if (late_added)
			return;

		late_added = true;

		Loom::ExtensionRegistry::AddPanel(panel_icon, late_panel_name, [](const std::string&) { });
	};
};

LOOM_EXTENSION_EXPORT int LoomExtensionAbi()
{
	return Loom::ExtensionAbi;
};

LOOM_EXTENSION_EXPORT void LoomExtensionStart()
{
	Loom::ExtensionRegistry::AddPanel(
		panel_icon,
		panel_name,
		[](const std::string&)
	{
		if (ImGui::Button(clicked ? clicked_label : waiting_label))
			clicked = true;

		if (file_used)
			ImGui::Button(file_used_label);

		if (custom_used)
			ImGui::Button(custom_used_label);
	});

#ifndef LOOM_FIXTURE_OLD_API
	Loom::ExtensionRegistry::AddMenuItem(file_menu, file_item, [](const std::string&)
	{
		file_used = true;
		AddLatePanel();
	});
	Loom::ExtensionRegistry::AddMenuItem(custom_menu, custom_item, [](const std::string&) { custom_used = true; });

	for (const char* menu : editor_menus)
		Loom::ExtensionRegistry::AddMenuItem(menu, editor_menu_item, [](const std::string&) { });
#endif
};

LOOM_EXTENSION_EXPORT void LoomExtensionStop()
{
};
