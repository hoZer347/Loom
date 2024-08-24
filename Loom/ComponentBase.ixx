export module ComponentBase;

import Engine;


namespace Loom
{
	export struct ComponentBase
	{
		virtual void Gui() = 0;

		const size_t id = Engine::id_counter++;
	};
};
