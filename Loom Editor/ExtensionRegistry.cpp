#include "ExtensionRegistry.h"

#include <utility>


namespace Loom
{
	void ExtensionRegistry::AddPanel(
		const std::string& icon,
		const std::string& name,
		std::function<void(const std::string& project)> draw)
	{
		Panels().push_back({ icon + " " + name + "###" + name, std::move(draw) });
	};

	std::deque<ExtensionRegistry::Panel>& ExtensionRegistry::Panels()
	{
		static std::deque<Panel> panels{ };

		return panels;
	};
};
