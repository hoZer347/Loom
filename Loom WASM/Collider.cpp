#include "Collider.h"

#include "Mesh.h"
#include "Component.h"
#include "GameObject.h"


namespace Loom
{
namespace Physics
{
	Collider::Collider() :
		m_mesh(m_gameObject->GetComponent<Mesh>())
	{
		
	};

	Collider::~Collider()
	{
		
	};

	void Collider::OnPhysics()
	{
		
	};
};
};
