#pragma once

#include <cstddef>
#include <string>
#include <vector>


namespace Loom
{
	struct GameObject;

	// How much of clip space a loaded model's longest side spans.
	inline constexpr float model_fit_extent = 1.6f;

	inline constexpr size_t model_floats_per_vertex = 3;
	inline constexpr size_t model_vertices_per_triangle = 3;

	// One mesh of a model, unindexed (which is how Mesh draws), with its node's
	// transform already applied.
	struct ModelPart
	{
		std::string name;
		std::vector<float> vertices;
	};

	// Whether Assimp has an importer for the file's extension.
	bool IsModelFile(const std::string& path);

	/**
	* Loom::LoadModel
	* - Reads the triangles of a model file through Assimp, one part per mesh
	* - Meshes draw in clip space, so the model is centred on the origin and
	*   scaled until its longest side spans model_fit_extent
	* - Empty, with the reason logged, when the file cannot be read
	*/
	std::vector<ModelPart> LoadModel(const std::string& path);

	/**
	* Loom::ImportModel
	* - Loads a model into a new child of parent, named after the file, with a
	*   Mesh and a Material for every part of it
	* - The vertices are copied into the scene rather than referenced, so the
	*   scene saves and loads without the model file beside it
	* - Null, with the reason logged, when the file cannot be read
	*/
	GameObject* ImportModel(const std::string& path, GameObject& parent);
};
