#include "Macro Helpers.h"
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
		 1,  1, 0.0f,
		 1, -1, 0.0f,
		-1, -1, 0.0f,
		-1,  1, 0.0f,
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

	Mesh* mesh = scene.Attach<Mesh>(GL_TRIANGLES);
	mesh->m_vertices = vertices;
	mesh->m_indices = indices;
	glPointSize(10);

	Engine::SetUpdateFunction(
		[&]()
		{
			static std::array<float, 2> screenDimensions;
			screenDimensions[0] = (float)Input::screen_width;
			screenDimensions[1] = (float)Input::screen_height;

			static std::array<float, 2> mousePosition;
			mousePosition[0] = (float)Input::mouse_x;
			mousePosition[1] = (float)Input::mouse_y;

			shader.SetShaderVec2(
				"screen_dimensions",
				&screenDimensions);

			shader.SetShaderVec2(
				"mouse_position",
				&mousePosition);
		});

	engine.Start();

	return 0;
};
