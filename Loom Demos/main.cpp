import Demos;

#include "Engine.h"
#include "Scene.h"

#include <vector>
#include <unordered_map>
#include <iostream>

#include "Mesh.h"
#include "Shaders.h"
#include "Material.h"
#include "Collider.h"

using namespace Loom;


GameObject* PrepCube(Scene* scene, std::string name)
{
	GameObject* go = scene->AddChild(name);

	Mesh* mesh = go->Attach<Mesh>(GL_QUADS);

	// Define 8 corners of the cube
	const float x0 = 0, x1 = 1;
	const float y0 = 0, y1 = 0.5f;
	const float z0 = 0, z1 = 1;

	// Define vertices for all 6 faces
	mesh->m_vertices =
	{
		// Front face
		x0, y0, z1,
		x1, y0, z1,
		x1, y1, z1,
		x0, y1, z1,

		// Back face
		x1, y0, z0,
		x0, y0, z0,
		x0, y1, z0,
		x1, y1, z0,

		// Left face
		x0, y0, z0,
		x0, y0, z1,
		x0, y1, z1,
		x0, y1, z0,

		// Right face
		x1, y0, z1,
		x1, y0, z0,
		x1, y1, z0,
		x1, y1, z1,

		// Top face
		x0, y1, z1,
		x1, y1, z1,
		x1, y1, z0,
		x0, y1, z0,

		// Bottom face
		x0, y0, z0,
		x1, y0, z0,
		x1, y0, z1,
		x0, y0, z1,
	};

	// 6 faces × 4 vertices each
	mesh->m_indices =
	{
		// Front
		0, 1, 2, 3,
		// Back
		4, 5, 6, 7,
		// Left
		8, 9, 10, 11,
		// Right
		12, 13, 14, 15,
		// Top
		16, 17, 18, 19,
		// Bottom
		20, 21, 22, 23,
	};

	Material* mat = go->Attach<Material>();
	mat->shader = new Shader("Shader.shader");

	return go;
};


int main()
{
	Engine engine;

	Scene scene;
	scene.Attach<DemosWindow>();

	auto* cube1 = PrepCube(&scene, "Cube1");
	//auto* cube2 = PrepCube(&scene, "Cube2");
	//auto* cube3 = PrepCube(&scene, "Cube3");
	//auto* cube4 = PrepCube(&scene, "Cube4");

	//for (const auto& [i, j] : Shader::shaders)
	//	std::cout << j << ": " << i << std::endl;

	engine.Start();

	return 0;
};
