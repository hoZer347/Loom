export module MainMenu;

namespace Loom
{
	export struct MainMenu
	{
		MainMenu() = delete;

	protected:
		friend struct Engine;

		static void Gui();
	};
};
