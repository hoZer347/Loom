import ComponentMenu;

#include "imgui.h"


namespace Loom
{
	void ComponentMenu::Gui()
	{
		if (subject)
		{
			ImGui::Begin("Component Menu");
			subject->Gui();
			for (auto& i : subject->components)
				i->Gui();
			ImGui::End();
		};
	};
};
