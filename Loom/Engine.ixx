export module Engine;

import DataTypes;


namespace Loom
{
	export struct Application;

	export struct Engine final
	{
		static void Init();
		static void Clean();

		Engine() = delete;
		~Engine() = delete;
	};
};
