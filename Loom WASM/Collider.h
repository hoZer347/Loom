#pragma once

#include "Component.h"


namespace Loom
{
	struct Mesh;

	namespace Physics
	{
		struct Collider : Component<Collider>
		{
			Collider();
			~Collider();

			void OnPhysics() override;

		private:
			Collider* m_head = nullptr;
			Mesh* m_mesh = nullptr;
		};

		struct CubeCollider
		{
		
		};
	};
};
