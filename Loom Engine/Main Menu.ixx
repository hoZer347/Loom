export module MainMenu;

import LoomObject;


namespace Loom
{
	export struct MainMenu :
		public LoomObject
	{
	protected:
		friend struct Engine;
		
		static void Gui();

	private:
		MainMenu() = delete;
	};
};
