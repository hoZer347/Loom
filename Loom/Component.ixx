export module Component;

import "imgui.h";
import Engine;
import ComponentBase;
import <string>;
import <typeinfo>;


namespace Loom
{
	export template <typename T>
		struct Component : public ComponentBase
	{
		operator ComponentBase& () { return *this; };

		void Gui()
		{
			std::string title = typeid(T).name();
			title += ", GUID: ";
			title += std::to_string(id);
		};
	};
};
