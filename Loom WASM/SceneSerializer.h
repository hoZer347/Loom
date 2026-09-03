#pragma once

#include "Loom API.h"

#include <string>


namespace Loom
{
	struct Scene;

	/**
	* Loom::SceneSerializer
	* - Reads and writes the scene format:
	*
	*		<scene name>(<guid>):
	*			GameObject <name>(<guid>)
	*				<field> = <value>
	*				Component <type>(<guid>)
	*					<field> = <value>
	*				GameObject <child name>(<guid>)
	*
	* - One tab per level of nesting. A line's depth says what owns it: fields
	*   belong to the object one level up, components to the GameObject one level
	*   up, and a GameObject nested under a GameObject is its child
	* - Objects are written with the guid they are carrying, and references
	*   between them are written as those guids, so a scene read back has the
	*   same shape and the same internal links as the one that was written
	*/
	struct LOOM_API SceneSerializer final
	{
		static constexpr const char* extension = ".loomscene";

		static std::string Serialize(Scene& scene);

		// Writes the scene to a file, creating the directories leading to it.
		static bool SaveToFile(Scene& scene, const std::string& path, std::string* error = nullptr);

		// Builds a new scene from the text. Returns null and fills in error when
		// the text is not a scene; anything smaller (a component this build does
		// not know, a field that no longer exists, a reference to something that
		// was never written) is reported to std::cerr and skipped, so an
		// out-of-date file still loads as far as it can.
		static Scene* Deserialize(const std::string& text, std::string* error = nullptr);
		static Scene* LoadFromFile(const std::string& path, std::string* error = nullptr);
	};
};
