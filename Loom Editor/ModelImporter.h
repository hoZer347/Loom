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

	// What a textured model draws with, relative to the project folder,
	// written there by the first import that needs it: unlit, sampling the
	// texture blended or, for a model whose texture asks for it, nearest.
	inline constexpr const char* model_textured_shader_path = "Assets/Textured.shader";
	inline constexpr const char* model_pixelated_shader_path = "Assets/Pixelated.shader";

	// The sampler2D those shaders draw, by name.
	inline constexpr const char* model_texture_variable = "u_texture";

	// One mesh of a model, unindexed (which is how Mesh draws), with its node's
	// transform already applied, and its normals when the file has them.
	struct ModelPart
	{
		std::string name;
		std::vector<float> vertices;
		std::vector<float> normals;
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
	* - A model with a skeleton or animations keeps its nodes, each a
	*   GameObject under the model, which is scaled and placed to span
	*   model_fit_extent as a static model is. Each part gets a Skin bound to
	*   the joints by name and its shape keys, and the model an Animator with
	*   every clip, playing the first. Its texture is copied out into
	*   Assets/<model name>/ and drawn with model_textured_shader_path, or
	*   model_pixelated_shader_path when the file samples it nearest
	* - Null, with the reason logged, when the file cannot be read
	*/
	GameObject* ImportModel(const std::string& path, GameObject& parent);
};
