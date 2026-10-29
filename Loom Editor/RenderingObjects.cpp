#include "RenderingObjects.h"

#include "GameObject.h"
#include "PixelShader.h"
#include "PreVertex.h"


namespace Loom
{
	GameObject* RenderingObjects::AddPreVertex(GameObject& parent, const std::string& file)
	{
		GameObject* gameObject = parent.AddChild(PreVertexLabel);
		gameObject->Attach<PreVertex>()->asset = file;

		return gameObject;
	};

	GameObject* RenderingObjects::AddPixelShader(GameObject& parent)
	{
		GameObject* gameObject = parent.AddChild(PixelShaderLabel);
		gameObject->Attach<PixelShader>();

		return gameObject;
	};
};
