import GameObject;
import ComponentBase;
import ComponentMenu;

#include "imgui.h"
import <string>;


namespace Loom
{
	void GameObject::Gui()
	{
		std::string title = typeid(*this).name();
		title += ", GUID: ";
		title += std::to_string(id);

		if (ImGui::TreeNode(title.c_str()))
		{
			if (ImGui::Button("New"))
				children.emplace_back(std::make_shared<GameObject>());

			if (ImGui::Button("Open"))
				ComponentMenu::subject = this;

			ImGui::Indent();
			for (auto& component : components)
				component->Gui();
			for (auto& child : children)
				child->Gui();
			ImGui::Unindent();

			ImGui::TreePop();
		};
	};
};
