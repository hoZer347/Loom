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
	inline constexpr size_t model_floats_per_uv = 2;
	inline constexpr size_t model_floats_per_color = 4;

	// What an imported model draws with, relative to the project folder,
	// written there by the first import: lit and shadowed, its texture times
	// its vertex colours times the material's colour.
	inline constexpr const char* model_shader_path = "Assets/Model.shader";

	// The material variables that shader reads, by name: the texture, white
	// without one, and the alpha below which a fragment is cut out.
	inline constexpr const char* model_texture_variable = "u_texture";
	inline constexpr const char* model_alpha_cutoff_variable = "u_alphaCutoff";

	// One mesh of a model, unindexed (which is how Mesh draws), with its node's
	// transform already applied, its UVs, normals and colours when the file
	// has them, and which of the file's materials it draws with.
	struct ModelPart
	{
		std::string name;
		std::vector<float> vertices;
		std::vector<float> uvs;
		std::vector<float> normals;
		std::vector<float> colors;
		unsigned int material = 0;
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
	*   Mesh and a Material for every part of it. Each Material draws with
	*   model_shader_path in the file's base colour, and its texture copied
	*   out into Assets/<model name>/, sampled nearest when the file says so
	* - The vertices are copied into the scene rather than referenced, so the
	*   scene saves and loads without the model file beside it
	* - A model with a skeleton or animations keeps its nodes, each a
	*   GameObject under the model, which is scaled and placed to span
	*   model_fit_extent as a static model is. Each part gets a Skin bound to
	*   the joints by name and its shape keys, and the model an Animator with
	*   every clip, playing the first
	* - Null, with the reason logged, when the file cannot be read
	*/
	GameObject* ImportModel(const std::string& path, GameObject& parent);
};
