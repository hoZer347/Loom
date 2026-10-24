#pragma once

#include "Loom API.h"

#include "Component.h"

#include <string>
#include <utility>
#include <vector>


namespace Loom
{
	/**
	* Loom::MissingComponent
	* - Stands in for a component whose type nothing has registered, such as a
	*   script whose library has not built, as Unity's missing script does
	* - Keeps the type it was written under and its field lines as text, and is
	*   written back under that type with those lines, so a scene saved while
	*   the type is missing loses nothing of it
	* - Once the type is registered, reading that text again makes the real
	*   component with its values
	* - Never in the Add Component menu: it cannot be built from nothing
	*/
	struct LOOM_API MissingComponent final : Component<MissingComponent>
	{
		// One field line as it was read: its key and everything after the '='.
		struct Line
		{
			std::string key;
			std::string value;
		};

		explicit MissingComponent(std::string type) :
			m_type(std::move(type))
		{ };

		// The registered name the component was written under.
		const std::string& GetType() const { return m_type; };

		// Written back in this order, under the component's line.
		std::vector<Line> lines{ };

	private:
		std::string m_type;
	};
};
