export module ComponentMenu;

import GameObject;
import <memory>;

namespace Loom
{
	export struct ComponentMenu final
	{
	protected:
		friend struct Engine;
		friend struct GameObject;
		static void Gui();

		static inline GameObject* subject;

	private:
		ComponentMenu() = delete;
		~ComponentMenu() = delete;
		ComponentMenu(const ComponentMenu&) = delete;

		static inline bool shouldRender = true;
	};
};
