#include "ModelImporter.h"

#include "Animator.h"
#include "GameObject.h"
#include "Material.h"
#include "Mesh.h"
#include "Skin.h"

#include "OpenGL.h"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
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

		// Four joints a vertex is what Skin bends by.
		constexpr unsigned int rigged_import_flags = import_flags | aiProcess_LimitBoneWeights;

		// glTF's code for a sampler that reads the nearest texel.
		constexpr int GLTF_NEAREST = 9728;

		// Closer than this to nothing, an offset is none: a shape key's vertex
		// that does not move, or a channel that never leaves its rest value.
		constexpr float NEGLIGIBLE = 1e-5f;

		// glTF's alpha cutoff when a material is masked and names none.
		constexpr float GLTF_DEFAULT_CUTOFF = 0.5f;

		constexpr const char* MODEL_SHADER = R"(// Written by the model importer: the texture times the vertex colours
// times the material's colour, lit and shadowed, and cut out where its
// alpha falls below the cutoff.

// ===SETTINGS===

shading = on
shadows = on

// ===VERTEX===

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 3) in vec4 aColor;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

out vec2 v_uv;
out vec4 v_color;

void main()
{
	v_uv = aUV;
	v_color = aColor;
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}

// ===FRAGMENT===

uniform sampler2D u_texture; // material
uniform float u_alphaCutoff; // material
uniform vec3 u_color;

in vec2 v_uv;
in vec4 v_color;

out vec4 FragColor;

void main()
{
	vec4 colour = texture(u_texture, v_uv) * v_color * vec4(u_color, 1.0);

	if (colour.a < u_alphaCutoff)
		discard;

	FragColor = vec4(colour.rgb, 1.0);
}
)";

		void Collect(
			const aiScene& scene,
			const aiNode& node,
			const aiMatrix4x4& parent_transform,
			std::vector<ModelPart>& parts)
		{
			const aiMatrix4x4 transform = parent_transform * node.mTransformation;

			// Keeps normals square to their faces however the node stretches them.
			const aiMatrix3x3 turn = aiMatrix3x3(transform).Inverse().Transpose();

			for (unsigned int n = 0; n < node.mNumMeshes; n++)
			{
				const aiMesh& mesh = *scene.mMeshes[node.mMeshes[n]];

				// Points and lines are sorted into meshes of their own, which a
				// triangle list has nothing to draw with.
				if (!(mesh.mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
					continue;

				ModelPart& part = parts.emplace_back();
				part.material = mesh.mMaterialIndex;

				part.name =
					node.mName.length ? node.mName.C_Str() :
					mesh.mName.length ? mesh.mName.C_Str() :
					"Mesh " + std::to_string(parts.size());

				part.vertices.reserve((size_t)mesh.mNumFaces * model_vertices_per_triangle * model_floats_per_vertex);

				if (mesh.HasNormals())
					part.normals.reserve(part.vertices.capacity());

				for (unsigned int f = 0; f < mesh.mNumFaces; f++)
				{
					const aiFace& face = mesh.mFaces[f];

					if (face.mNumIndices != model_vertices_per_triangle)
						continue;

					for (unsigned int i = 0; i < face.mNumIndices; i++)
					{
						const aiVector3D vertex = transform * mesh.mVertices[face.mIndices[i]];

						part.vertices.insert(part.vertices.end(), { vertex.x, vertex.y, vertex.z });

						if (mesh.HasTextureCoords(0))
						{
							const aiVector3D& uv = mesh.mTextureCoords[0][face.mIndices[i]];

							part.uvs.insert(part.uvs.end(), { uv.x, uv.y });
						};

						if (mesh.HasNormals())
						{
							const aiVector3D normal = (turn * mesh.mNormals[face.mIndices[i]]).Normalize();

							part.normals.insert(part.normals.end(), { normal.x, normal.y, normal.z });
						};

						if (mesh.HasVertexColors(0))
						{
							const aiColor4D& color = mesh.mColors[0][face.mIndices[i]];

							part.colors.insert(part.colors.end(), { color.r, color.g, color.b, color.a });
						};
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

		bool HasRig(const aiScene& scene)
		{
			if (scene.HasAnimations())
				return true;

			for (unsigned int m = 0; m < scene.mNumMeshes; m++)
				if (scene.mMeshes[m]->HasBones() || scene.mMeshes[m]->mNumAnimMeshes)
					return true;

			return false;
		};

		glm::vec3 ToGlm(const aiVector3D& vector)
		{
			return glm::vec3(vector.x, vector.y, vector.z);
		};

		glm::quat ToGlm(const aiQuaternion& rotation)
		{
			return glm::quat(rotation.w, rotation.x, rotation.y, rotation.z);
		};

		// Assimp's matrices are row by row, glm's column by column.
		glm::mat4 ToGlm(const aiMatrix4x4& matrix)
		{
			return glm::transpose(glm::make_mat4(&matrix.a1));
		};

		void SetLocal(GameObject& object, const aiMatrix4x4& matrix)
		{
			aiVector3D scaling, position;
			aiQuaternion rotation;
			matrix.Decompose(scaling, rotation, position);

			object.transform.position = ToGlm(position);
			object.transform.rotation = Transform::EulerDegrees(ToGlm(rotation));
			object.transform.scale = ToGlm(scaling);
		};

		// model_shader_path, written into the project if it has none by that name.
		std::string ModelShader()
		{
			if (!std::filesystem::exists(model_shader_path))
			{
				std::filesystem::create_directories(std::filesystem::path(model_shader_path).parent_path());
				std::ofstream(model_shader_path, std::ios::binary) << MODEL_SHADER;
			};

			return model_shader_path;
		};

		// The material's texture as a file in folder, copied out of the model or
		// from beside it: its base colour, or failing that its diffuse or
		// emission. Empty when it has none. nearest says whether the file asks
		// for the texture to be sampled nearest.
		std::string TextureOf(
			const aiScene& scene,
			const aiMaterial& material,
			const std::filesystem::path& model,
			const std::filesystem::path& folder,
			bool& nearest)
		{
			for (const aiTextureType type : { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE, aiTextureType_EMISSIVE })
			{
				aiString file;

				if (material.GetTexture(type, 0, &file) != AI_SUCCESS)
					continue;

				int filter = 0;
				nearest = material.Get(AI_MATKEY_GLTF_MAPPINGFILTER_MAG(type, 0), filter) == AI_SUCCESS && filter == GLTF_NEAREST;

				std::filesystem::create_directories(folder);

				if (const aiTexture* embedded = scene.GetEmbeddedTexture(file.C_Str()))
				{
					// Texels rather than an image file's bytes, which only some
					// formats embed and stb has nothing to read them with.
					if (embedded->mHeight != 0)
						continue;

					const std::string stem = embedded->mFilename.length
						? std::filesystem::path(embedded->mFilename.C_Str()).stem().string()
						: material.GetName().C_Str();

					const std::filesystem::path target = folder / (stem + "." + embedded->achFormatHint);
					std::ofstream(target, std::ios::binary).write((const char*)embedded->pcData, embedded->mWidth);

					return target.generic_string();
				};

				const std::filesystem::path source = model.parent_path() / file.C_Str();
				const std::filesystem::path target = folder / source.filename();
				std::error_code code;

				if (std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, code))
					return target.generic_string();
			};

			return { };
		};

		// The file's material as Model.shader draws it: its base colour, its
		// texture copied into folder, how that is sampled, and where it is cut out.
		void Dress(
			Material& drawn,
			const aiScene& scene,
			const aiMaterial& material,
			const std::filesystem::path& model,
			const std::filesystem::path& folder)
		{
			drawn.SetShaderPath(ModelShader());

			aiColor4D base(1.0f, 1.0f, 1.0f, 1.0f);

			// Drawn by its glow's texture alone, as an unlit export is, it takes
			// the glow's colour, its base colour being black.
			const bool glows = material.GetTextureCount(aiTextureType_BASE_COLOR) + material.GetTextureCount(aiTextureType_DIFFUSE) == 0
				&& material.GetTextureCount(aiTextureType_EMISSIVE) > 0;

			if (glows)
				material.Get(AI_MATKEY_COLOR_EMISSIVE, base);
			else if (material.Get(AI_MATKEY_BASE_COLOR, base) != AI_SUCCESS)
				material.Get(AI_MATKEY_COLOR_DIFFUSE, base);

			drawn.color = glm::vec3(base.r, base.g, base.b);

			bool nearest = false;
			const std::string texture = TextureOf(scene, material, model, folder, nearest);

			if (!texture.empty())
				drawn.variables->SetTexture(model_texture_variable, texture);

			if (nearest)
				drawn.sampling = Material::Nearest;

			aiString mode;

			if (material.Get(AI_MATKEY_GLTF_ALPHAMODE, mode) == AI_SUCCESS && std::string_view(mode.C_Str()) == "MASK")
			{
				float cutoff = GLTF_DEFAULT_CUTOFF;
				material.Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff);

				drawn.variables->Set(model_alpha_cutoff_variable, UniformType::Float, { cutoff });
			};
		};

		// What a clip or a shape key is called, by its place when the file gives it no name.
		std::string ClipName(const aiAnimation& animation, unsigned int index)
		{
			return animation.mName.length ? animation.mName.C_Str() : "Clip " + std::to_string(index + 1);
		};

		std::string ShapeName(const aiAnimMesh& shape, unsigned int index)
		{
			return shape.mName.length ? shape.mName.C_Str() : "Shape " + std::to_string(index + 1);
		};

		std::string JoinLines(const std::vector<std::string>& lines)
		{
			std::string joined;

			for (const std::string& line : lines)
				joined += line + '\n';

			return joined;
		};

		// A mesh unindexed, which is how Mesh draws, with what a Skin needs to
		// bend it: each vertex's joints and weights and each shape key's
		// offsets, by the unindexed vertex they move.
		struct SkinnedPart
		{
			std::vector<float> vertices;
			std::vector<float> uvs;
			std::vector<float> normals;
			std::vector<std::string> joints;
			std::vector<float> inverseBinds;
			std::vector<float> influences;
			std::vector<std::string> shapes;
			std::vector<float> shapeOffsets;
		};

		SkinnedPart Unindex(const aiMesh& mesh)
		{
			SkinnedPart part;

			// Each vertex's joints, then their weights; LimitBoneWeights has left
			// no vertex with more than a Skin takes.
			std::vector<std::array<float, Skin::FLOATS_PER_INFLUENCE>> influences(mesh.mNumVertices);
			std::vector<size_t> taken(mesh.mNumVertices);

			for (unsigned int b = 0; b < mesh.mNumBones; b++)
			{
				const aiBone& bone = *mesh.mBones[b];
				const glm::mat4 inverseBind = ToGlm(bone.mOffsetMatrix);

				part.joints.push_back(bone.mName.C_Str());
				part.inverseBinds.insert(part.inverseBinds.end(), glm::value_ptr(inverseBind), glm::value_ptr(inverseBind) + Skin::MATRIX_FLOATS);

				for (unsigned int w = 0; w < bone.mNumWeights; w++)
				{
					const aiVertexWeight& weight = bone.mWeights[w];
					size_t& slot = taken[weight.mVertexId];

					if (slot == Skin::JOINTS_PER_VERTEX)
						continue;

					influences[weight.mVertexId][slot] = (float)b;
					influences[weight.mVertexId][Skin::JOINTS_PER_VERTEX + slot] = weight.mWeight;
					slot++;
				};
			};

			std::vector<unsigned int> order;

			for (unsigned int f = 0; f < mesh.mNumFaces; f++)
			{
				const aiFace& face = mesh.mFaces[f];

				if (face.mNumIndices == model_vertices_per_triangle)
					order.insert(order.end(), face.mIndices, face.mIndices + face.mNumIndices);
			};

			for (const unsigned int index : order)
			{
				const aiVector3D& vertex = mesh.mVertices[index];
				part.vertices.insert(part.vertices.end(), { vertex.x, vertex.y, vertex.z });

				if (mesh.HasTextureCoords(0))
					part.uvs.insert(part.uvs.end(), { mesh.mTextureCoords[0][index].x, mesh.mTextureCoords[0][index].y });

				if (mesh.HasNormals())
					part.normals.insert(part.normals.end(), { mesh.mNormals[index].x, mesh.mNormals[index].y, mesh.mNormals[index].z });

				if (mesh.HasBones())
					part.influences.insert(part.influences.end(), influences[index].begin(), influences[index].end());
			};

			for (unsigned int a = 0; a < mesh.mNumAnimMeshes; a++)
			{
				const aiAnimMesh& shape = *mesh.mAnimMeshes[a];

				part.shapes.push_back(ShapeName(shape, a));

				const size_t count = part.shapeOffsets.size();
				part.shapeOffsets.push_back(0.0f);

				for (size_t v = 0; v < order.size(); v++)
				{
					const aiVector3D offset = shape.mVertices[order[v]] - mesh.mVertices[order[v]];

					if (offset.Length() <= NEGLIGIBLE)
						continue;

					part.shapeOffsets.insert(part.shapeOffsets.end(), { (float)v, offset.x, offset.y, offset.z });
					part.shapeOffsets[count]++;
				};
			};

			return part;
		};

		struct RiggedModel
		{
			const aiScene& scene;
			std::filesystem::path path;
			std::filesystem::path textures;
			GameObject* model;
		};

		void AttachPart(RiggedModel& rigged, const aiMesh& mesh, GameObject& object)
		{
			SkinnedPart part = Unindex(mesh);

			Dress(*object.Attach<Material>(), rigged.scene, *rigged.scene.mMaterials[mesh.mMaterialIndex], rigged.path, rigged.textures);

			Mesh* drawing = object.Attach<Mesh>(Mesh::Triangles);
			drawing->m_vertices = std::move(part.vertices);
			drawing->m_textureCoordinates = std::move(part.uvs);
			drawing->m_normals = std::move(part.normals);

			if (part.joints.empty() && part.shapes.empty())
				return;

			// Bent again every frame.
			drawing->m_draw_type = Mesh::Dynamic;

			Skin* skin = object.Attach<Skin>();
			skin->skeleton = rigged.model;
			skin->m_joints = JoinLines(part.joints);
			skin->m_inverseBinds = std::move(part.inverseBinds);
			skin->m_influences = std::move(part.influences);
			skin->m_shapes = JoinLines(part.shapes);
			skin->m_shapeOffsets = std::move(part.shapeOffsets);
			skin->m_shapeWeights = std::vector<float>(part.shapes.size());
		};

		void AttachNode(RiggedModel& rigged, const aiNode& node, GameObject& object)
		{
			std::vector<const aiMesh*> meshes;

			for (unsigned int n = 0; n < node.mNumMeshes; n++)
				if (const aiMesh* mesh = rigged.scene.mMeshes[node.mMeshes[n]]; mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)
					meshes.push_back(mesh);

			// One part on the node itself; more, each under it, named after its material.
			for (const aiMesh* mesh : meshes)
				AttachPart(
					rigged,
					*mesh,
					meshes.size() == 1 ? object : *object.AddChild(rigged.scene.mMaterials[mesh->mMaterialIndex]->GetName().C_Str()));

			for (unsigned int c = 0; c < node.mNumChildren; c++)
			{
				const aiNode& child = *node.mChildren[c];
				GameObject* added = object.AddChild(child.mName.C_Str());

				SetLocal(*added, child.mTransformation);
				AttachNode(rigged, child, *added);
			};
		};

		void Bound(const aiScene& scene, const aiNode& node, const aiMatrix4x4& parent, glm::vec3& low, glm::vec3& high)
		{
			const aiMatrix4x4 transform = parent * node.mTransformation;

			for (unsigned int n = 0; n < node.mNumMeshes; n++)
			{
				const aiMesh& mesh = *scene.mMeshes[node.mMeshes[n]];

				for (unsigned int v = 0; v < mesh.mNumVertices; v++)
				{
					const glm::vec3 vertex = ToGlm(transform * mesh.mVertices[v]);
					low = glm::min(low, vertex);
					high = glm::max(high, vertex);
				};
			};

			for (unsigned int c = 0; c < node.mNumChildren; c++)
				Bound(scene, *node.mChildren[c], transform, low, high);
		};

		// A channel of one clip: its property, what it moves, and its keys, each
		// a time and then the value.
		struct Track
		{
			size_t clip;
			Animator::Property property;
			std::string target;
			std::vector<float> keys;
		};

		void AddKeys(Track& track, double time, std::initializer_list<float> value)
		{
			track.keys.push_back((float)time);
			track.keys.insert(track.keys.end(), value);
		};

		// Whether every key holds rest, which a quaternion does at either sign.
		bool HoldsRest(const Track& track, const std::vector<float>& rest)
		{
			const size_t floats = Animator::ValueFloats(track.property);

			for (size_t at = 0; at < track.keys.size(); at += 1 + floats)
			{
				float difference = 0.0f;

				if (track.property == Animator::Property::Rotation)
				{
					const float* key = track.keys.data() + at + 1;
					const float dot = key[0] * rest[0] + key[1] * rest[1] + key[2] * rest[2] + key[3] * rest[3];
					difference = 1.0f - std::abs(dot);
				}
				else for (size_t i = 0; i < floats; i++)
					difference = std::max(difference, std::abs(track.keys[at + 1 + i] - rest[i]));

				if (difference > NEGLIGIBLE)
					return false;
			};

			return true;
		};

		// Whether every key is the first, so one key says it all.
		bool Constant(const Track& track)
		{
			const size_t stride = 1 + Animator::ValueFloats(track.property);

			for (size_t at = stride; at < track.keys.size(); at += stride)
				for (size_t i = 1; i < stride; i++)
					if (std::abs(track.keys[at + i] - track.keys[i]) > NEGLIGIBLE)
						return false;

			return true;
		};

		// The shape key names of a morph channel's mesh: the meshes on the node
		// it names, or failing that the mesh by that name.
		std::vector<std::string> ShapeNames(const aiScene& scene, const aiString& name)
		{
			const aiMesh* found = nullptr;

			if (const aiNode* node = scene.mRootNode->FindNode(name); node && node->mNumMeshes)
				found = scene.mMeshes[node->mMeshes[0]];

			for (unsigned int m = 0; m < scene.mNumMeshes && !found; m++)
				if (scene.mMeshes[m]->mName == name)
					found = scene.mMeshes[m];

			std::vector<std::string> names;

			for (unsigned int a = 0; found && a < found->mNumAnimMeshes; a++)
				names.push_back(ShapeName(*found->mAnimMeshes[a], a));

			return names;
		};

		// Every clip's channels into an Animator. A channel that holds its rest
		// value in every clip is left out; one that moves in any clip is in all
		// of them, a clip without it holding it at rest, so switching clips
		// never leaves it where the last one moved it.
		void AttachAnimator(const aiScene& scene, GameObject& model)
		{
			std::vector<Track> tracks;
			std::map<std::pair<std::string, Animator::Property>, std::vector<float>> rests;
			std::string clips;

			for (unsigned int a = 0; a < scene.mNumAnimations; a++)
			{
				const aiAnimation& animation = *scene.mAnimations[a];
				const double ticks = animation.mTicksPerSecond > 0.0 ? animation.mTicksPerSecond : 1.0;
				const std::string name = ClipName(animation, a);

				clips += name + '\t' + std::to_string(animation.mDuration / ticks) + '\n';

				for (unsigned int c = 0; c < animation.mNumChannels; c++)
				{
					const aiNodeAnim& channel = *animation.mChannels[c];
					const std::string target = channel.mNodeName.C_Str();
					const aiNode* node = scene.mRootNode->FindNode(channel.mNodeName);

					if (!node)
						continue;

					aiVector3D scaling, position;
					aiQuaternion rotation;
					node->mTransformation.Decompose(scaling, rotation, position);

					Track& moved = tracks.emplace_back(Track{ a, Animator::Property::Translation, target });

					for (unsigned int k = 0; k < channel.mNumPositionKeys; k++)
					{
						const aiVector3D& value = channel.mPositionKeys[k].mValue;
						AddKeys(moved, channel.mPositionKeys[k].mTime / ticks, { value.x, value.y, value.z });
					};

					Track& turned = tracks.emplace_back(Track{ a, Animator::Property::Rotation, target });

					for (unsigned int k = 0; k < channel.mNumRotationKeys; k++)
					{
						const aiQuaternion& value = channel.mRotationKeys[k].mValue;
						AddKeys(turned, channel.mRotationKeys[k].mTime / ticks, { value.x, value.y, value.z, value.w });
					};

					Track& sized = tracks.emplace_back(Track{ a, Animator::Property::Scale, target });

					for (unsigned int k = 0; k < channel.mNumScalingKeys; k++)
					{
						const aiVector3D& value = channel.mScalingKeys[k].mValue;
						AddKeys(sized, channel.mScalingKeys[k].mTime / ticks, { value.x, value.y, value.z });
					};

					rests[{ target, Animator::Property::Translation }] = { position.x, position.y, position.z };
					rests[{ target, Animator::Property::Rotation }] = { rotation.x, rotation.y, rotation.z, rotation.w };
					rests[{ target, Animator::Property::Scale }] = { scaling.x, scaling.y, scaling.z };
				};

				for (unsigned int m = 0; m < animation.mNumMorphMeshChannels; m++)
				{
					const aiMeshMorphAnim& channel = *animation.mMorphMeshChannels[m];
					const std::vector<std::string> shapes = ShapeNames(scene, channel.mName);

					for (size_t shape = 0; shape < shapes.size(); shape++)
					{
						Track& weighed = tracks.emplace_back(Track{ a, Animator::Property::Weight, shapes[shape] });
						rests[{ shapes[shape], Animator::Property::Weight }] = { 0.0f };

						for (unsigned int k = 0; k < channel.mNumKeys; k++)
						{
							const aiMeshMorphKey& key = channel.mKeys[k];
							float weight = 0.0f;

							for (unsigned int v = 0; v < key.mNumValuesAndWeights; v++)
								if (key.mValues[v] == shape)
									weight = (float)key.mWeights[v];

							AddKeys(weighed, key.mTime / ticks, { weight });
						};
					};
				};
			};

			std::set<std::pair<std::string, Animator::Property>> moving;

			for (const Track& track : tracks)
				if (!HoldsRest(track, rests[{ track.target, track.property }]))
					moving.insert({ track.target, track.property });

			for (unsigned int a = 0; a < scene.mNumAnimations; a++)
				for (const auto& [target, property] : moving)
				{
					const bool keyed = std::any_of(
						tracks.begin(),
						tracks.end(),
						[&](const Track& track) { return track.clip == a && track.target == target && track.property == property; });

					if (keyed)
						continue;

					Track& held = tracks.emplace_back(Track{ a, property, target });
					held.keys.push_back(0.0f);
					held.keys.insert(held.keys.end(), rests[{ target, property }].begin(), rests[{ target, property }].end());
				};

			std::string channels;
			std::vector<float> keys;

			for (Track& track : tracks)
			{
				if (track.keys.empty() || !moving.contains({ track.target, track.property }))
					continue;

				if (Constant(track))
					track.keys.resize(1 + Animator::ValueFloats(track.property));

				channels += std::to_string(track.clip) + '\t' + Animator::PropertyName(track.property) + '\t' + track.target + '\n';
				keys.push_back((float)(track.keys.size() / (1 + Animator::ValueFloats(track.property))));
				keys.insert(keys.end(), track.keys.begin(), track.keys.end());
			};

			Animator* animator = model.Attach<Animator>();
			animator->m_clips = clips;
			animator->m_channels = channels;
			animator->m_keys = std::move(keys);
		};

		GameObject* ImportRigged(const aiScene& scene, const std::string& path, GameObject& parent)
		{
			constexpr float infinity = std::numeric_limits<float>::infinity();

			glm::vec3 low(infinity), high(-infinity);
			Bound(scene, *scene.mRootNode, aiMatrix4x4(), low, high);

			const glm::vec3 size = high - low;
			const float longest = std::max({ size.x, size.y, size.z });
			const float scale = longest > 0.0f ? model_fit_extent / longest : 1.0f;

			const std::string name = std::filesystem::path(path).stem().string();
			GameObject* model = parent.AddChild(name);
			model->transform.scale = glm::vec3(scale);
			model->transform.position = -(low + high) * 0.5f * scale;

			RiggedModel rigged{ scene, path, std::filesystem::path("Assets") / name, model };

			GameObject* root = model->AddChild(scene.mRootNode->mName.length ? scene.mRootNode->mName.C_Str() : name);
			SetLocal(*root, scene.mRootNode->mTransformation);
			AttachNode(rigged, *scene.mRootNode, *root);

			if (scene.HasAnimations())
				AttachAnimator(scene, *model);

			return model;
		};

		// The scene's triangles, each mesh with its node's transform applied,
		// centred and scaled to span model_fit_extent. Empty, with why logged,
		// when there are none.
		std::vector<ModelPart> PartsOf(const aiScene& scene, const std::string& path)
		{
			std::vector<ModelPart> parts;
			Collect(scene, *scene.mRootNode, aiMatrix4x4(), parts);

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

		return PartsOf(*scene, path);
	};

	GameObject* ImportModel(const std::string& path, GameObject& parent)
	{
		Assimp::Importer importer;

		const aiScene* scene = importer.ReadFile(path.c_str(), rigged_import_flags);

		if (scene == nullptr || scene->mRootNode == nullptr)
		{
			std::cerr << "Import: " << path << ": " << importer.GetErrorString() << std::endl;
			return nullptr;
		};

		if (HasRig(*scene))
			return ImportRigged(*scene, path, parent);

		std::vector<ModelPart> parts = PartsOf(*scene, path);

		if (parts.empty())
			return nullptr;

		const std::string name = std::filesystem::path(path).stem().string();
		const std::filesystem::path textures = std::filesystem::path("Assets") / name;

		GameObject* model = parent.AddChild(name);

		for (ModelPart& part : parts)
		{
			// A single mesh goes on the model itself rather than under it.
			GameObject* target = parts.size() == 1 ? model : model->AddChild(part.name);

			Dress(*target->Attach<Material>(), *scene, *scene->mMaterials[part.material], path, textures);

			Mesh* drawing = target->Attach<Mesh>(Mesh::Triangles);
			drawing->m_vertices = std::move(part.vertices);
			drawing->m_textureCoordinates = std::move(part.uvs);
			drawing->m_normals = std::move(part.normals);
			drawing->m_colors = std::move(part.colors);
		};

		return model;
	};
};
