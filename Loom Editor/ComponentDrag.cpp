#include "ComponentDrag.h"

#include "Component.h"
#include "GameObject.h"
#include "LoomObject.h"

#include <cmath>


namespace Loom::ComponentDrag
{
	ComponentBase* Dragged(const GameObject& gameObject, const Guid& guid)
	{
		ComponentBase* component = LoomObject::GetByGuid<ComponentBase>(guid);

		return component != nullptr && component->GetGameObject() == &gameObject ? component : nullptr;
	};

	bool Header(const char* label, ImGuiTreeNodeFlags flags, bool folded)
	{
		if (!folded)
			return ImGui::CollapsingHeader(label, flags);

		ImGuiStorage* storage = ImGui::GetStateStorage();
		const ImGuiID id = ImGui::GetID(label);
		const int was_open = storage->GetInt(id, (flags & ImGuiTreeNodeFlags_DefaultOpen) != 0);

		ImGui::SetNextItemOpen(false);
		ImGui::CollapsingHeader(label, flags);
		storage->SetInt(id, was_open);

		return false;
	};

	Scroll Step(const ImVec2& mouse, const ImRect& inner, float row, float dt, float carried)
	{
		if (mouse.x < inner.Min.x || mouse.x > inner.Max.x)
			return { 0.0f, carried };

		const float edge = edge_rows * row;
		const float step = dt * rows_per_second * row;

		if (mouse.y < inner.Min.y + edge)
			carried -= step;
		else if (mouse.y > inner.Max.y - edge)
			carried += step;
		else
			return { 0.0f, carried };

		const float whole = std::trunc(carried);

		return { whole, carried - whole };
	};
};
