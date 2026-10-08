#include "EditorGui.h"

#include "imgui.h"


namespace Loom
{
	bool InputTextString(const char* label, std::string& value)
	{
		const auto resize =
			[](ImGuiInputTextCallbackData* data) -> int
			{
				if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
					return 0;

				std::string* text = (std::string*)data->UserData;
				text->resize(data->BufTextLen);
				data->Buf = text->data();

				return 0;
			};

		return ImGui::InputText(
			label,
			value.data(),
			value.capacity() + 1,
			ImGuiInputTextFlags_CallbackResize,
			resize,
			&value);
	};

	bool AcceptAssetDrop(std::string& path)
	{
		if (!ImGui::BeginDragDropTarget())
			return false;

		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ASSET_PAYLOAD);

		if (payload)
			path = (const char*)payload->Data;

		ImGui::EndDragDropTarget();

		return payload != nullptr;
	};
};
