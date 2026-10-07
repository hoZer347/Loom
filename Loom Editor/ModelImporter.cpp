#include "ModelImporter.h"

#include "ProjectTemplate.h"

#include "GameObject.h"
#include "Material.h"
#include "Mesh.h"

#include "OpenGL.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <unordered_set>
#include <vector>


namespace Loom
{
	namespace
	{
		constexpr unsigned int import_flags =
			aiProcess_Triangulate |
			aiProcess_SortByPType;

		void Collect(
			const aiScene& scene,
			const aiNode& node,
			const aiMatrix4x4& parent_transform,
			std::vector<ModelPart>& parts)
		{
			const aiMatrix4x4 transform = parent_transform * node.mTransformation;

			for (unsigned int n = 0; n < node.mNumMeshes; n++)
			{
				const aiMesh& mesh = *scene.mMeshes[node.mMeshes[n]];

				// Points and lines are sorted into meshes of their own, which a
				// triangle list has nothing to draw with.
				if (!(mesh.mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
					continue;

				ModelPart& part = parts.emplace_back();

				part.name =
					node.mName.length ? node.mName.C_Str() :
					mesh.mName.length ? mesh.mName.C_Str() :
					"Mesh " + std::to_string(parts.size());

				part.vertices.reserve((size_t)mesh.mNumFaces * model_vertices_per_triangle * model_floats_per_vertex);

				for (unsigned int f = 0; f < mesh.mNumFaces; f++)
				{
					const aiFace& face = mesh.mFaces[f];

					if (face.mNumIndices != model_vertices_per_triangle)
						continue;

					for (unsigned int i = 0; i < face.mNumIndices; i++)
					{
						const aiVector3D vertex = transform * mesh.mVertices[face.mIndices[i]];

						part.vertices.insert(part.vertices.end(), { vertex.x, vertex.y, vertex.z });
					};
				};
			};

			for (unsigned int c = 0; c < node.mNumChildren; c++)
				Collect(scene, *node.mChildren[c], transform, parts);
		};

		// Lower case, dot first, the way std::filesystem gives an extension.
		const std::unordered_set<std::string>& ModelExtensions()
		{
			static const std::unordered_set<std::string> extensions = []
			{
				// "*.3ds;*.obj;..."
				std::string list;
				Assimp::Importer().GetExtensionList(list);

				std::unordered_set<std::string> found;
				std::istringstream patterns(list);

				for (std::string pattern; std::getline(patterns, pattern, ';');)
					found.insert(pattern.substr(pattern.find('.')));

				return found;
			}();

			return extensions;
		};
	};

	bool IsModelFile(const std::string& path)
	{
		std::string extension = std::filesystem::path(path).extension().string();

		std::transform(
			extension.begin(),
			extension.end(),
			extension.begin(),
			[](unsigned char c) { return (char)std::tolower(c); });

		return ModelExtensions().contains(extension);
	};

	std::vector<ModelPart> LoadModel(const std::string& path)
	{
		Assimp::Importer importer;

		const aiScene* scene = importer.ReadFile(path.c_str(), import_flags);

		if (scene == nullptr || scene->mRootNode == nullptr)
		{
			std::cerr << "Import: " << path << ": " << importer.GetErrorString() << std::endl;
			return { };
		};

		std::vector<ModelPart> parts;
		Collect(*scene, *scene->mRootNode, aiMatrix4x4(), parts);

		if (parts.empty())
		{
			std::cerr << "Import: " << path << " has no triangles" << std::endl;
			return { };
		};

		constexpr float infinity = std::numeric_limits<float>::infinity();

		float low[model_floats_per_vertex] = { infinity, infinity, infinity };
		float high[model_floats_per_vertex] = { -infinity, -infinity, -infinity };

		for (const ModelPart& part : parts)
			for (size_t i = 0; i < part.vertices.size(); i++)
			{
				const size_t axis = i % model_floats_per_vertex;

				low[axis] = std::min(low[axis], part.vertices[i]);
				high[axis] = std::max(high[axis], part.vertices[i]);
			};

		float centre[model_floats_per_vertex];
		float longest = 0.0f;

		for (size_t axis = 0; axis < model_floats_per_vertex; axis++)
		{
			centre[axis] = std::midpoint(low[axis], high[axis]);
			longest = std::max(longest, high[axis] - low[axis]);
		};

		const float scale = longest > 0.0f ? model_fit_extent / longest : 1.0f;

		for (ModelPart& part : parts)
			for (size_t i = 0; i < part.vertices.size(); i++)
				part.vertices[i] = (part.vertices[i] - centre[i % model_floats_per_vertex]) * scale;

		return parts;
	};

	GameObject* ImportModel(const std::string& path, GameObject& parent)
	{
		std::vector<ModelPart> parts = LoadModel(path);

		if (parts.empty())
			return nullptr;

		if (!std::filesystem::exists(ProjectTemplate::shader_path))
			std::cerr << "Import: no " << ProjectTemplate::shader_path << " in the project, so the model will not draw" << std::endl;

		GameObject* model = parent.AddChild(std::filesystem::path(path).stem().string());

		for (ModelPart& part : parts)
		{
			// A single mesh goes on the model itself rather than under it.
			GameObject* target = parts.size() == 1 ? model : model->AddChild(part.name);

			target->Attach<Material>()->SetShaderPath(ProjectTemplate::shader_path);
			target->Attach<Mesh>((uint32_t)GL_TRIANGLES)->m_vertices = std::move(part.vertices);
		};

		std::cout << "Imported " << path << " (" << parts.size() << " mesh(es))" << std::endl;

		return model;
	};
};
