#include "RenderingObjects.h"

#include "Camera.h"
#include "ComputeShader.h"
#include "GameObject.h"
#include "Light.h"
#include "PixelShader.h"
#include "PreVertex.h"
#include "Shapes.h"

#include <cmath>


namespace Loom
{
	namespace
	{
		// A starter plane's size, and its camera's pitch in degrees and distance from the plane's middle.
		constexpr float STARTER_PLANE_SCALE = 10.0f;
		constexpr float STARTER_CAMERA_PITCH = -45.0f;
		constexpr float STARTER_CAMERA_DISTANCE = 8.0f;
	};

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

	GameObject* RenderingObjects::AddLight(GameObject& parent)
	{
		GameObject* gameObject = parent.AddChild(LightLabel);
		gameObject->Attach<Light>();
		gameObject->transform.rotation = Light::RotationAlong(Light::DEFAULT_DIRECTION);

		return gameObject;
	};

	GameObject* RenderingObjects::AddCamera(GameObject& parent)
	{
		GameObject* gameObject = parent.AddChild(CameraLabel);
		gameObject->Attach<Camera>();

		return gameObject;
	};

	void RenderingObjects::AddStarter(GameObject& root, const std::string& shader)
	{
		AddLight(root);

		const float height = STARTER_CAMERA_DISTANCE * std::sin(glm::radians(-STARTER_CAMERA_PITCH));
		const float back = STARTER_CAMERA_DISTANCE * std::cos(glm::radians(-STARTER_CAMERA_PITCH));

		GameObject* camera = AddCamera(root);
		camera->transform.position = glm::vec3(0.0f, height, back);
		camera->transform.rotation = glm::vec3(STARTER_CAMERA_PITCH, 0.0f, 0.0f);

		GameObject* plane = Shapes::Add(root, Shapes::Shape::Plane, shader);
		plane->transform.scale = glm::vec3(STARTER_PLANE_SCALE);
	};

	GameObject* RenderingObjects::AddComputeShader(GameObject& parent)
	{
		GameObject* gameObject = parent.AddChild(ComputeShaderLabel);
		gameObject->Attach<ComputeShader>();

		return gameObject;
	};
};
