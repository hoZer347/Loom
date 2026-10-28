#include "RenderingObjects.h"

#include "GameObject.h"
#include "PreVertex.h"


namespace Loom
{
	GameObject* RenderingObjects::AddPreVertex(GameObject& parent, const std::string& file)
	{
		GameObject* gameObject = parent.AddChild(PreVertexLabel);
		gameObject->Attach<PreVertex>()->file = file;

		return gameObject;
	};
};
