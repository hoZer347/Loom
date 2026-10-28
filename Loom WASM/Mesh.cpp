#include "Mesh.h"

#include "Camera.h"
#include "Engine.h"
#include "OpenGL.h"
#include "Renderer.h"
#include "Shaders.h"
#include "Material.h"
#include "PixelPass.h"
#include "GameObject.h"
#include "Jobs.h"
#include "Profiler.h"
#include "Scene.h"
#include "Skin.h"
#include "Utilities/Clock.h"

#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <bit>
#include <climits>
#include <cstring>
#include <iostream>
#include <optional>
#include <utility>


namespace Loom
{
	static_assert(Mesh::Points == GL_POINTS);
	static_assert(Mesh::Lines == GL_LINES);
	static_assert(Mesh::LineLoop == GL_LINE_LOOP);
	static_assert(Mesh::LineStrip == GL_LINE_STRIP);
	static_assert(Mesh::Triangles == GL_TRIANGLES);
	static_assert(Mesh::TriangleStrip == GL_TRIANGLE_STRIP);
	static_assert(Mesh::TriangleFan == GL_TRIANGLE_FAN);

	static_assert(Mesh::Stream == GL_STREAM_DRAW);
	static_assert(Mesh::Static == GL_STATIC_DRAW);
	static_assert(Mesh::Dynamic == GL_DYNAMIC_DRAW);

	namespace
	{
		// Floats a vertex's position takes in m_vertices.
		constexpr size_t POSITION_COMPONENTS = 3;

		#pragma region Instances

		// What each instance MeshBatch hands over holds: its model matrix, its colour, then its seed.
		constexpr size_t MODEL_FLOATS = sizeof(glm::mat4) / sizeof(float);
		constexpr size_t COLOR_FLOATS = sizeof(glm::vec3) / sizeof(float);
		constexpr size_t SEED_FLOATS = 1;

		static_assert(MODEL_FLOATS + COLOR_FLOATS + SEED_FLOATS == Renderer::FloatsPerInstance);

		#pragma endregion

		#pragma region Hashing

		// splitmix64's finaliser.
		constexpr uint64_t SPLITMIX_FIRST = 0xBF58476D1CE4E5B9ull;
		constexpr uint64_t SPLITMIX_SECOND = 0x94D049BB133111EBull;
		constexpr int SPLITMIX_SHIFT_FIRST = 30;
		constexpr int SPLITMIX_SHIFT_SECOND = 27;
		constexpr int SPLITMIX_SHIFT_LAST = 31;

		// As many of the hash's top bits as a float holds exactly, over one past the most they make.
		constexpr int SEED_BITS = 24;
		constexpr float SEED_SCALE = 1.0f / (float)(1u << SEED_BITS);

		// 64 bit FNV-1a's.
		constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
		constexpr uint64_t FNV_PRIME = 1099511628211ull;

		// FNV-1a, a 32 bit value at a time.
		uint64_t Mixed(uint64_t hash, uint32_t value)
		{
			return (hash ^ value) * FNV_PRIME;
		};

		// u_seed from the guid, so a saved GameObject keeps its seed.
		float Seed(const GameObject& gameObject)
		{
			const Guid& guid = gameObject.GetGuid();
			uint64_t hash = guid.high ^ guid.low;

			hash = (hash ^ (hash >> SPLITMIX_SHIFT_FIRST)) * SPLITMIX_FIRST;
			hash = (hash ^ (hash >> SPLITMIX_SHIFT_SECOND)) * SPLITMIX_SECOND;
			hash ^= hash >> SPLITMIX_SHIFT_LAST;

			return (float)(hash >> (sizeof(hash) * CHAR_BIT - SEED_BITS)) * SEED_SCALE;
		};

		uint64_t Hashed(const std::vector<float>& vertices, const VertexLayout& layout)
		{
			uint64_t hash = Mixed(FNV_OFFSET_BASIS, (uint32_t)layout.format);

			for (const UVLayout& layer : layout.layers)
				hash = Mixed(Mixed(hash, layer.id), layer.words);

			for (const float value : vertices)
				hash = Mixed(hash, std::bit_cast<uint32_t>(value));

			return hash;
		};

		#pragma endregion

		#pragma region UV Layers

		// Each layer starts with its id, its words a vertex, then its vertices.
		constexpr size_t LAYER_ID = 0;
		constexpr size_t LAYER_WORDS = 1;
		constexpr size_t LAYER_VERTICES = 2;
		constexpr size_t LAYER_HEADER = 3;

		// Calls found with each whole layer and where its words start and end.
		template <typename Found>
		void EachLayer(const std::vector<unsigned int>& words, const Found& found)
		{
			for (size_t at = 0; at + LAYER_HEADER <= words.size(); )
			{
				const size_t length = (size_t)words[at + LAYER_WORDS] * words[at + LAYER_VERTICES];
				const size_t first = at + LAYER_HEADER;

				if (length > words.size() - first)
					return;

				found(words[at + LAYER_ID], words[at + LAYER_WORDS], first, first + length);
				at = first + length;
			};
		};

		// As many whole vertices as the layer holds.
		void AppendLayer(std::vector<unsigned int>& words, const UVLayer& layer)
		{
			const size_t vertices = layer.words ? layer.values.size() / layer.words : 0;

			words.insert(words.end(), { layer.id, layer.words, (unsigned int)vertices });
			words.insert(words.end(), layer.values.begin(), layer.values.begin() + vertices * layer.words);
		};

		// The layer's span with its id, or where it would go.
		std::pair<size_t, size_t> LayerAt(const std::vector<unsigned int>& words, uint32_t id)
		{
			std::optional<std::pair<size_t, size_t>> at;
			size_t end = 0;

			EachLayer(words, [&](uint32_t other, uint32_t, size_t first, size_t last)
			{
				if (!at && other >= id)
					at = { first - LAYER_HEADER, other == id ? last : first - LAYER_HEADER };

				end = last;
			});

			return at.value_or(std::pair{ end, end });
		};

		#pragma endregion
	};

	#pragma region Construction

	Mesh::Mesh(Primitive primitive)
	{
		primitive_id = primitive;
	};

	Mesh::~Mesh()
	{
		// A renderer gone took what it kept with it.
		if (m_kept && m_keeper == Renderer::Get())
			m_keeper->DestroyVertices(m_kept);
	};

	#pragma endregion

	#pragma region Drawing

	bool Mesh::IsDrawable()
	{
		if (material == nullptr)
			material = m_gameObject->GetComponent<Material>();

		// No material yet, as with a mesh just added in the editor.
		if (material == nullptr)
			return false;

		material->PickUpShader();

		return material->shader != nullptr;
	};

	void Mesh::OnRender()
	{
		if (!IsDrawable())
			return;

		if (transparent && material->shader->settings.transparency)
		{
			transparent->push_back(this);
			return;
		};

		const uint32_t program = ProgramFor(material->shader->id, material->shader->skinned_id);

		if (batch && batch->Add(*this, program, material->shader->instanced_id))
			return;

		Ready(*this, program);
		Draw(program);
	};

	void Mesh::Ready(Mesh& mesh, uint32_t program)
	{
		mesh.material->Apply(program, &*std::as_const(mesh.variables));
		Scene::ApplyView(program);
		PixelPass::Note(**mesh.material);
	};

	void Mesh::Draw(uint32_t program)
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer || !Keep(*renderer))
			return;

		renderer->SetUniform(program, "u_model", m_gameObject->WorldMatrix());
		renderer->SetUniform(program, "u_seed", Seed(*m_gameObject));

		if (m_handed.skinned)
			renderer->SetUniforms(program, "u_joints", m_joints.data(), m_joints.size());

		renderer->DrawVertices(program, *std::as_const(primitive_id), m_kept);
	};

	uint32_t Mesh::ProgramFor(uint32_t program, uint32_t skinned)
	{
		return GpuSkin() ? skinned : program;
	};

	Skin* Mesh::GpuSkin()
	{
		Skin* skin = m_gameObject->GetComponent<Skin>();

		if (!skin || !IsDrawable())
			return nullptr;

		const Shader& shader = *material->shader;

		if (!shader.skinned_id || (shader.shadow_id && !shader.shadow_skinned_id) || !skin->FitsGpu(*this))
			return nullptr;

		return skin;
	};

	bool Mesh::Keep(Renderer& renderer)
	{
		if (m_indices.size())
		{
			// Indexed drawing is not wired up yet, so it warns once.
			static bool warned = false;

			if (!warned)
			{
				warned = true;
				std::cerr << "Mesh: indexed drawing is not supported yet" << std::endl;
			};

			return false;
		};

		constexpr size_t UV_COMPONENTS = 2;
		constexpr size_t NORMAL_COMPONENTS = 3;
		constexpr size_t COLOR_COMPONENTS = 4;

		// Enough vertices a job that handing them out costs little beside copying them.
		constexpr size_t VERTEX_GRAIN = 1024;

		Skin* const gpuSkin = GpuSkin();

		if (Skin* skin = m_gameObject->GetComponent<Skin>())
		{
			if (m_deformed.size() != std::as_const(m_vertices)->size() || m_deformedFrame != Time::FrameCount())
			{
				if (gpuSkin ? skin->Shape(*this) : skin->Deform(*this))
					m_deformedVersion++;

				m_deformedFrame = Time::FrameCount();

				if (gpuSkin)
					m_joints = skin->JointMatrices(*this);
				else m_joints.clear();
			};
		}
		else
		{
			m_deformed.clear();
			m_deformedNormals.clear();
			m_joints.clear();
		};

		// Read through const, so looking does not count as changing them.
		const std::vector<float>& vertices = *std::as_const(m_vertices);
		const std::vector<float>& textureCoordinates = *std::as_const(m_textureCoordinates);
		const std::vector<float>& declaredNormals = *std::as_const(m_normals);
		const std::vector<float>& colors = *std::as_const(m_colors);

		const bool deformed = m_deformed.size() == vertices.size();

		const Handed handed{
			m_vertices.Version(),
			m_textureCoordinates.Version(),
			m_normals.Version(),
			m_colors.Version(),
			m_uvLayers.Version(),
			m_draw_type.Version(),
			deformed,
			deformed ? m_deformedVersion : 0,
			gpuSkin != nullptr,
			gpuSkin ? gpuSkin->m_influences.Version() : 0,
			gpuSkin ? gpuSkin->m_joints.Version() : 0 };

		if (m_keeper != &renderer)
		{
			m_keeper = &renderer;
			m_kept = 0;
		};

		if (m_kept == 0 || handed != m_handed)
		{
			LOOM_STAGE("Mesh::Keep");

			if (m_kept == 0)
				m_kept = renderer.CreateVertices();

			m_handed = handed;

			const std::vector<float>& positions = deformed ? m_deformed : vertices;
			const std::vector<float>& normals = m_deformedNormals.size() == declaredNormals.size() ? m_deformedNormals : declaredNormals;
			const size_t count = positions.size() / POSITION_COMPONENTS;
			const bool hasUVs = textureCoordinates.size() == count * UV_COMPONENTS;
			const bool hasNormals = normals.size() == count * NORMAL_COMPONENTS;
			const bool hasColors = colors.size() == count * COLOR_COMPONENTS;

			VertexLayout layout = hasColors
				? VertexFormat::PositionUVNormalColor
				: hasNormals
					? VertexFormat::PositionUVNormal
					: hasUVs
						? VertexFormat::PositionUV
						: VertexFormat::Position;

			// Each layer with words for every vertex, read straight from m_uvLayers.
			struct Drawn
			{
				uint32_t words;
				const unsigned int* values;
			};

			const std::vector<unsigned int>& words = *std::as_const(m_uvLayers);
			std::vector<Drawn> layers;

			EachLayer(words, [&](uint32_t id, uint32_t perVertex, size_t first, size_t last)
			{
				if (id != Skin::UV_LAYER && perVertex && last - first == count * perVertex)
				{
					layers.push_back({ perVertex, words.data() + first });
					layout.layers.push_back({ id, perVertex });
				};
			});

			const std::vector<uint32_t> influences = gpuSkin ? gpuSkin->GpuInfluences() : std::vector<uint32_t>();

			if (gpuSkin)
			{
				layers.push_back({ (uint32_t)Skin::FLOATS_PER_INFLUENCE, influences.data() });
				layout.layers.push_back({ Skin::UV_LAYER, (uint32_t)Skin::FLOATS_PER_INFLUENCE });
			};

			// Only vertices that can join a batch are worth hashing.
			const bool batchable = *std::as_const(m_draw_type) == Static && !deformed;

			if (layout == VertexFormat::Position)
			{
				renderer.SetVertices(m_kept, positions.data(), count, *std::as_const(m_draw_type), layout);
				m_geometry = batchable ? Hashed(positions, layout) : 0;
			}
			else
			{
				constexpr float NO_NORMAL[NORMAL_COMPONENTS] = { };
				const size_t stride = FloatsPerVertex(layout);

				std::vector<float> handedOnce;
				std::vector<float>& interleaved = deformed && !gpuSkin ? m_interleaved : handedOnce;
				interleaved.resize(count * stride);

				if (!deformed || gpuSkin)
					m_interleaved = { };

				Jobs::For(count, VERTEX_GRAIN,
					[&](size_t, size_t begin, size_t end)
					{
						float* out = interleaved.data() + begin * stride;

						for (size_t i = begin; i < end; i++)
						{
							out = std::copy_n(positions.data() + i * POSITION_COMPONENTS, POSITION_COMPONENTS, out);

							// Without texture coordinates, aUV reads the position's x and y.
							if (layout.format != VertexFormat::Position)
								out = std::copy_n(hasUVs ? textureCoordinates.data() + i * UV_COMPONENTS : positions.data() + i * POSITION_COMPONENTS, UV_COMPONENTS, out);

							// With colours and no normals, aNormal reads zeros.
							if (hasNormals)
								out = std::copy_n(normals.data() + i * NORMAL_COMPONENTS, NORMAL_COMPONENTS, out);
							else if (hasColors)
								out = std::copy_n(NO_NORMAL, NORMAL_COMPONENTS, out);

							if (hasColors)
								out = std::copy_n(colors.data() + i * COLOR_COMPONENTS, COLOR_COMPONENTS, out);

							// Copied as bytes, so each word's bits arrive as they were.
							for (const Drawn& layer : layers)
							{
								std::memcpy(out, layer.values + i * layer.words, layer.words * sizeof(uint32_t));
								out += layer.words;
							};
						};
					});

				renderer.SetVertices(m_kept, interleaved.data(), count, *std::as_const(m_draw_type), layout);
				m_geometry = batchable ? Hashed(interleaved, layout) : 0;
			};
		};

		return true;
	};

	void Mesh::DrawTransparent(const std::vector<Mesh*>& meshes)
	{
		Renderer* renderer = Renderer::Get();

		if (meshes.empty() || !renderer)
			return;

		// Clip space depth does not depend on the aspect ratio.
		constexpr float ANY_ASPECT = 1.0f;

		const glm::mat4 viewProjection = Camera::current
			? Camera::current->ViewProjection(ANY_ASPECT)
			: glm::mat4(1.0f);

		std::vector<std::pair<float, Mesh*>> byDepth;
		byDepth.reserve(meshes.size());

		for (Mesh* mesh : meshes)
			byDepth.emplace_back((viewProjection * mesh->m_gameObject->WorldMatrix()[3]).z, mesh);

		std::stable_sort(byDepth.begin(), byDepth.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

		// Depth first, so only the nearest surface blends over what is behind.
		renderer->SetDepthTestEqual(true);

		for (const auto& [depth, mesh] : byDepth)
		{
			renderer->SetColorWrite(false);
			mesh->OnRender();
			renderer->SetColorWrite(true);
			mesh->OnRender();
		};

		renderer->SetDepthTestEqual(false);
	};

	#pragma endregion

	#pragma region UV Layers

	void Mesh::SetUVWords(const UVLayer& layer)
	{
		std::vector<unsigned int>& words = *m_uvLayers;
		const auto [first, last] = LayerAt(words, layer.id);
		const size_t vertices = layer.words ? layer.values.size() / layer.words : 0;
		const size_t length = LAYER_HEADER + vertices * layer.words;

		if (length > last - first)
			words.insert(words.begin() + last, length - (last - first), 0u);
		else
			words.erase(words.begin() + first + length, words.begin() + last);

		words[first + LAYER_ID] = layer.id;
		words[first + LAYER_WORDS] = layer.words;
		words[first + LAYER_VERTICES] = (unsigned int)vertices;
		std::copy_n(layer.values.begin(), vertices * layer.words, words.begin() + first + LAYER_HEADER);
	};

	void Mesh::SetUVLayers(std::vector<UVLayer> layers)
	{
		std::ranges::sort(layers, { }, &UVLayer::id);

		std::vector<unsigned int>& words = *m_uvLayers;
		words.clear();

		for (const UVLayer& layer : layers)
			AppendLayer(words, layer);
	};

	void Mesh::ClearUV(uint32_t id)
	{
		std::vector<unsigned int>& words = *m_uvLayers;
		const auto [first, last] = LayerAt(words, id);

		words.erase(words.begin() + first, words.begin() + last);
	};

	std::vector<UVLayer> Mesh::UVLayers() const
	{
		const std::vector<unsigned int>& words = *std::as_const(m_uvLayers);
		std::vector<UVLayer> layers;

		EachLayer(words, [&](uint32_t id, uint32_t perVertex, size_t first, size_t last)
		{
			if (perVertex)
				layers.push_back({ id, perVertex, { words.begin() + first, words.begin() + last } });
		});

		return layers;
	};

	#pragma endregion

	#pragma region Editor

	bool Mesh::IsSaved(const SerializedField& field) const
	{
		if (!m_generated)
			return true;

		return field.data != &*m_vertices &&
			field.data != &*m_textureCoordinates &&
			field.data != &*m_normals &&
			field.data != &*m_colors &&
			field.data != &*m_uvLayers;
	};

	bool Mesh::IsShown(const SerializedField& field) const
	{
		if (field.data == &*material)
		{
			const Material* own = m_gameObject->GetComponent<Material>();

			return own == nullptr || *std::as_const(material) != own;
		};

		return field.data != &*m_vertices &&
			field.data != &*m_textureCoordinates &&
			field.data != &*m_normals &&
			field.data != &*m_colors &&
			field.data != &*m_uvLayers;
	};

	std::string Mesh::Summary() const
	{
		const size_t vertices = std::as_const(m_vertices)->size() / POSITION_COMPONENTS;

		std::string summary = std::to_string(vertices) + (vertices == 1 ? " vertex" : " vertices");

		if (!m_indices.empty())
			summary += ", " + std::to_string(m_indices.size()) + " indices";

		if (!std::as_const(m_textureCoordinates)->empty())
			summary += ", UVs";

		if (!std::as_const(m_normals)->empty())
			summary += ", normals";

		if (!std::as_const(m_colors)->empty())
			summary += ", colours";

		for (const UVLayer& layer : UVLayers())
			summary += ", UV_" + std::to_string(layer.id);

		if (m_generated)
			summary += ", generated";

		return summary;
	};

	void Mesh::OnGui()
	{
		ImGui::TextDisabled("%s", Summary().c_str());

		if (!IsDrawable())
			return;

		const std::vector<ShaderVariable>& declared = material->shader->variables;

		const bool any = std::any_of(
			declared.begin(),
			declared.end(),
			[](const ShaderVariable& variable) { return variable.scope == UniformScope::Instance; });

		if (!any)
			return;

		ImGui::SeparatorText("Instance Variables");
		UniformValues values = variables;

		if (DrawShaderVariables(declared, UniformScope::Instance, values))
			variables = std::move(values);
	};

	#pragma endregion

	#pragma region Batching

	bool MeshBatch::Add(Mesh& mesh, uint32_t program, uint32_t instanced)
	{
		Renderer* renderer = Renderer::Get();

		if (!instanced ||
			!renderer ||
			*std::as_const(mesh.m_draw_type) != Mesh::Static ||
			!mesh.Keep(*renderer) ||
			mesh.m_handed.deformed)
			return false;

		const uint64_t key = Mixed(Mixed(mesh.m_geometry, program), *std::as_const(mesh.primitive_id));
		const auto [first, last] = m_byKey.equal_range(key);

		for (auto found = first; found != last; ++found)
			if (Alike(m_lots[found->second], mesh, program))
			{
				m_lots[found->second].meshes.push_back(&mesh);
				return true;
			};

		m_byKey.emplace(key, m_lots.size());
		m_lots.push_back({ program, instanced, { &mesh } });

		return true;
	};

	bool MeshBatch::Alike(const Lot& lot, const Mesh& mesh, uint32_t program)
	{
		const Mesh& first = *lot.meshes.front();
		const Material& a = **first.material;
		const Material& b = **mesh.material;

		// All Material::Apply sets but the colour, which each instance has its own of.
		const bool materials = &a == &b || (*a.shadows == *b.shadows && *a.sampling == *b.sampling && *a.variables == *b.variables);

		return lot.program == program &&
			first.m_geometry == mesh.m_geometry &&
			*first.primitive_id == *mesh.primitive_id &&
			materials &&
			*first.variables == *mesh.variables;
	};

	void MeshBatch::Draw(const Ready& ready)
	{
		Renderer* renderer = Renderer::Get();

		for (Lot& lot : m_lots)
		{
			Mesh& first = *lot.meshes.front();

			if (lot.meshes.size() == 1)
			{
				ready(first, lot.program);
				first.Draw(lot.program);
				continue;
			};

			m_instances.clear();

			for (const Mesh* mesh : lot.meshes)
			{
				const glm::mat4 model = mesh->m_gameObject->WorldMatrix();
				const float* color = glm::value_ptr(*std::as_const(mesh->material->color));

				m_instances.insert(m_instances.end(), glm::value_ptr(model), glm::value_ptr(model) + MODEL_FLOATS);
				m_instances.insert(m_instances.end(), color, color + COLOR_FLOATS);
				m_instances.push_back(Seed(*mesh->m_gameObject));
			};

			ready(first, lot.instanced);
			renderer->DrawInstances(lot.instanced, *std::as_const(first.primitive_id), first.m_kept, m_instances.data(), lot.meshes.size());
		};

		m_lots.clear();
		m_byKey.clear();
	};

	#pragma endregion
};
