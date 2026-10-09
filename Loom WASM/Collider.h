#pragma once

#include "Component.h"


namespace Loom
{
	namespace Physics
	{
		struct Collider : Component<Collider>
		{
			static constexpr const char* category = "Physics";

			~Collider();

			void OnPhysics() override;

		private:
			Collider* m_head = nullptr;
		};

		struct CubeCollider
		{
		
		};
	};
};
