export module MainMenu;

import LoomObject;


namespace Loom
{
	export struct MainMenu :
		public LoomObject
	{
		MainMenu() = delete;

		virtual ~MainMenu() = default;

	protected:
		friend struct Engine;
		
		static void Gui();
	};
};
