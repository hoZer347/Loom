#include "Engine.h"
#include "Scene.h"
#include "GameObject.h"
#include "Shaders.h"
#include "Material.h"
#include "OpenGL.h"
#include "Globals.h"
#include "Input.h"
#include "Mesh.h"

#include <array>

using namespace Loom;


int main()
{
	Engine engine;
	engine.Init();

	std::vector<float> vertices
	{
		 0.5f,  0.5f, 0.0f,
		 0.5f, -0.5f, 0.0f,
		-0.5f, -0.5f, 0.0f,
		-0.5f,  0.5f, 0.0f,
	};

	std::vector<uint32_t> indices
	{
		0, 1, 3,
		1, 2, 3
	};

	Scene scene;

	Shader shader{ "Shader" };

	Material* material = scene.Attach<Material>();
	material->m_shader = &shader;

	Mesh* mesh = scene.Attach<Mesh>();
	mesh->m_vertices = vertices;
	mesh->m_indices = indices;


	std::vector<float> view
	{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};

	std::vector<float> perspective
	{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};

	Engine::SetUpdateFunction(
		[&]()
		{
			static std::array<float, 2> screenDims;
			screenDims[0] = Input::screen_width;
			screenDims[1] = Input::screen_height;

			shader.SetShaderVec2(
				"screen_dimensions",
				&screenDims);


			static std::array<float, 2> mousePosition;
			mousePosition[0] = Input::mouse_x;
			mousePosition[1] = Input::mouse_y;

			shader.SetShaderVec2(
				"mouse_position",
				&mousePosition);


			shader.SetShaderMat4(
				"mvp",
				transform.data());
		});

	engine.Start();

	return 0;
};
