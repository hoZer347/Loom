#pragma once

#include "Loom API.h"

#include <string>


namespace Loom
{
	// What the Project panel hands over when a file is dragged out of it: the
	// file's path relative to the project folder, forward slashes, with its
	// terminating null.
	inline constexpr const char* ASSET_PAYLOAD = "LoomAsset";

	// ImGui's InputText over a std::string, which ImGui itself only has in the
	// stdlib helper.
	LOOM_API bool InputTextString(const char* label, std::string& value);

	// Makes the last item a drop target for files dragged out of the Project
	// panel, and puts a dropped file's path in path. Whether one was dropped.
	LOOM_API bool AcceptAssetDrop(std::string& path);
};
