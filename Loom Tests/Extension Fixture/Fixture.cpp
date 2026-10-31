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


namespace
{
	constexpr const char* panel_icon = "F";
	constexpr const char* panel_name = "Reload Fixture";
	constexpr const char* waiting_label = "Fixture Waiting##Fixture";
	constexpr const char* clicked_label = "Fixture Clicked##Fixture";

	bool clicked = false;
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
	});
};

LOOM_EXTENSION_EXPORT void LoomExtensionStop()
{
};
