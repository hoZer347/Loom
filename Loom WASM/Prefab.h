#pragma once

#include "Loom API.h"

#include "Component.h"
#include "Serial.h"

#include <string>


namespace Loom
{
	/**
	* Loom::Prefab
	* - Marks the GameObject it is on as an instance of a prefab: a GameObject
	*   saved on its own as a .loomprefab file, by its path from the project
	*   folder
	* - The instance is written into its scene in full, so a game loads it like
	*   any other GameObject and never reads the prefab file. Keeping instances
	*   and the file the same is the editor's job
	*/
	struct LOOM_API Prefab : Component<Prefab, "Prefabs">
	{
		static constexpr const char* extension = ".loomprefab";

		Serial<std::string> path;
	};
};
