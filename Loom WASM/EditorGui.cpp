#include "EditorGui.h"

#include "imgui.h"


namespace Loom
{
	namespace
	{
		int Resize(ImGuiInputTextCallbackData* data)
		{
			if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
				return 0;

			std::string* text = (std::string*)data->UserData;
			text->resize(data->BufTextLen);
			data->Buf = text->data();

			return 0;
		};
	};

	bool InputTextString(const char* label, std::string& value)
	{
		return ImGui::InputText(
			label,
			value.data(),
			value.capacity() + 1,
			ImGuiInputTextFlags_CallbackResize,
			Resize,
			&value);
	};

	bool InputTextMultilineString(const char* label, std::string& value, float height)
	{
		return ImGui::InputTextMultiline(
			label,
			value.data(),
			value.capacity() + 1,
			ImVec2(-FLT_MIN, height),
			ImGuiInputTextFlags_CallbackResize,
			Resize,
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
