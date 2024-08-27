import GameObject;

#include "imgui.h"


namespace Loom
{
	GameObject* GameObject::AddChild(const char* name)
	{
		std::lock_guard lock{ mutex };

		return children.emplace_back(new GameObject(name));
	};

	void GameObject::_Update()
	{
		std::lock_guard lock{ mutex };

		Update();
		for (Component* component : components)
			component->Update();
		for (GameObject* child : children)
			child->_Update();
	};

	void GameObject::_Gui()
	{
		if (ImGui::TreeNode((void*)this, (std::string(name) + " (" + std::to_string(id) + ")").c_str()))
		{
			ImGui::SameLine();
			if (ImGui::Button("Add"))
			{
				std::lock_guard lock{ mutex };

				children.push_back(new GameObject((char*)"New GameObject"));
			};

			ImGui::Text("Name: ");
			ImGui::SameLine();

			if (ImGui::InputText(std::to_string(id).c_str(), newName, 128))
				name = newName;

			for (Component* component : components)
				component->OnGui();
			for (GameObject* child : children)
				child->_Gui();

			ImGui::TreePop();
		};
	};
};
