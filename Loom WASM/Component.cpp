#include "Component.h"

#include "ComponentRegistry.h"

#include "imgui.h"


namespace Loom
{
	void ComponentBase::DrawDefaultGui()
	{
		ImGui::PushID(this);

		if (ImGui::TreeNode((void*)this, "%s", ComponentRegistry::NameOf(*this).c_str()))
		{
			OnGui();
			ImGui::TreePop();
		};

		ImGui::PopID();
	};
};
