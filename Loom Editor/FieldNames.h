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
	* - A Serial of an enum type also gets that enum's enumerators, labelled the
	*   same way, which is what the inspector offers in its dropdown
	* - Worked out once per type and module build, which also covers a script
	*   library rebuilt with a different layout under the same type name
	*/
	struct FieldNames final
	{
		struct Enumerator final
		{
			std::string label;
			long long value = 0;
		};

		// One label per entry of object.GetFields(), in the same order.
		static std::vector<std::string> Of(const LoomObject& object);

		// One list per entry of object.GetFields(), null for a field that is not
		// an enum or that the symbols say nothing about. The lists live as long
		// as the process.
		static std::vector<const std::vector<Enumerator>*> EnumeratorsOf(const LoomObject& object);
	};
};
