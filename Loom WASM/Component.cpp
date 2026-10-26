#include "Component.h"

#include "ComponentRegistry.h"

#include "imgui.h"


namespace Loom
{
	#pragma region Gui

	void ComponentBase::DrawDefaultGui()
	{
		ImGui::PushID(this);

		if (ImGui::TreeNode((void*)this, "%s", ComponentRegistry::NameOf(*this).c_str()))
		{
			OnGui();
			ExtraGui();
			ImGui::TreePop();
		};

		ImGui::PopID();
	};

	#pragma endregion
};
