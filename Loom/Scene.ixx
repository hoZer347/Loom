export module Scene;

import Engine;
import GameObject;

import <thread>;


namespace Loom
{
	export struct Scene :
		public GameObject
	{
		Scene()
		{ };

		virtual ~Scene()
		{ };

		void Gui() override;

		std::thread thread;
		std::shared_ptr<GameObject> head;
	};
};
