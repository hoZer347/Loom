#pragma once

#include "LoomObject.h"

#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::FieldNames
	* - What the inspector calls each of an object's Serial members: the
	*   member's own identifier, read out of the debug symbols of the module
	*   that defined the type, so m_speed reads "Speed"
	* - A field nothing in the symbols matches, or a module built without them,
	*   reads "Field <n>" instead
	* - Worked out once per type and module build, which also covers a script
	*   library rebuilt with a different layout under the same type name
	*/
	struct FieldNames final
	{
		// One label per entry of object.GetFields(), in the same order.
		static std::vector<std::string> Of(const LoomObject& object);
	};
};
