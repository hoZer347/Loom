#include "Mesh.h"

#include "Camera.h"
#include "Engine.h"
#include "OpenGL.h"
#include "Renderer.h"
#include "Shaders.h"
#include "Material.h"
#include "PixelPass.h"
#include "GameObject.h"
#include "Scene.h"
#include "Skin.h"
#include "Utilities/Clock.h"

#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <bit>
#include <iostream>
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
		// What each instance MeshBatch hands over holds: its model matrix, then its colour.
		constexpr size_t MODEL_FLOATS = sizeof(glm::mat4) / sizeof(float);
		constexpr size_t COLOR_FLOATS = sizeof(glm::vec3) / sizeof(float);

		static_assert(MODEL_FLOATS + COLOR_FLOATS == Renderer::FloatsPerInstance);

		// 64 bit FNV-1a's.
		constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
		constexpr uint64_t FNV_PRIME = 1099511628211ull;

		// FNV-1a, a 32 bit value at a time.
		uint64_t Mixed(uint64_t hash, uint32_t value)
		{
			return (hash ^ value) * FNV_PRIME;
		};

		uint64_t Hashed(const std::vector<float>& vertices, VertexFormat format)
		{
			uint64_t hash = Mixed(FNV_OFFSET_BASIS, (uint32_t)format);

			for (const float value : vertices)
				hash = Mixed(hash, std::bit_cast<uint32_t>(value));

			return hash;
		};
	};

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

	bool Mesh::IsSaved(const SerializedField& field) const
	{
		if (!m_generated)
			return true;

		return field.data != &*m_vertices && field.data != &*m_textureCoordinates && field.data != &*m_normals && field.data != &*m_colors;
	};

	bool Mesh::IsDrawable()
	{
		if (material == nullptr)
			material = m_gameObject->GetComponent<Material>();

		// A mesh attached without a material (or before one is set up, which is
		// the normal state for a mesh just added in the editor) has nothing to
		// draw with.
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

		const uint32_t program = material->shader->id;

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
		renderer->DrawVertices(program, *std::as_const(primitive_id), m_kept);
	};

	bool Mesh::Keep(Renderer& renderer)
	{
		if (m_indices.size())
		{
			// Indexed drawing is not wired up yet, and saying so once beats
			// either drawing nonsense or throwing out of the frame loop.
			static bool warned = false;

			if (!warned)
			{
				warned = true;
				std::cerr << "Mesh: indexed drawing is not supported yet" << std::endl;
			};

			return false;
		};

		constexpr size_t POSITION_COMPONENTS = 3;
		constexpr size_t UV_COMPONENTS = 2;
		constexpr size_t NORMAL_COMPONENTS = 3;
		constexpr size_t COLOR_COMPONENTS = 4;

		if (Skin* skin = m_gameObject->GetComponent<Skin>())
		{
			if (m_deformed.size() != std::as_const(m_vertices)->size() || m_deformedFrame != Time::FrameCount())
			{
				skin->Deform(*this);
				m_deformedFrame = Time::FrameCount();
			};
		}
		else
		{
			m_deformed.clear();
			m_deformedNormals.clear();
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
			m_draw_type.Version(),
			deformed,
			deformed ? m_deformedFrame : 0 };

		if (m_keeper != &renderer)
		{
			m_keeper = &renderer;
			m_kept = 0;
		};

		if (m_kept == 0 || handed != m_handed)
		{
			if (m_kept == 0)
				m_kept = renderer.CreateVertices();

			m_handed = handed;

			const std::vector<float>& positions = deformed ? m_deformed : vertices;
			const std::vector<float>& normals = m_deformedNormals.size() == declaredNormals.size() ? m_deformedNormals : declaredNormals;
			const size_t count = positions.size() / POSITION_COMPONENTS;
			const bool hasUVs = textureCoordinates.size() == count * UV_COMPONENTS;
			const bool hasNormals = normals.size() == count * NORMAL_COMPONENTS;
			const bool hasColors = colors.size() == count * COLOR_COMPONENTS;

			// Only vertices that can join a batch are worth hashing.
			const bool batchable = *std::as_const(m_draw_type) == Static && !deformed;

			if (!hasUVs && !hasNormals && !hasColors)
			{
				renderer.SetVertices(m_kept, positions.data(), count, *std::as_const(m_draw_type), VertexFormat::Position);
				m_geometry = batchable ? Hashed(positions, VertexFormat::Position) : 0;
			}
			else
			{
				const VertexFormat format = hasColors
					? VertexFormat::PositionUVNormalColor
					: hasNormals
						? VertexFormat::PositionUVNormal
						: VertexFormat::PositionUV;
				constexpr float NO_NORMAL[NORMAL_COMPONENTS] = { };
				std::vector<float> interleaved(count * FloatsPerVertex(format));
				float* out = interleaved.data();

				for (size_t i = 0; i < count; i++)
				{
					// Without texture coordinates, aUV reads the position's x and y,
					// as it does from vertices drawn without normals.
					out = std::copy_n(positions.data() + i * POSITION_COMPONENTS, POSITION_COMPONENTS, out);
					out = std::copy_n(hasUVs ? textureCoordinates.data() + i * UV_COMPONENTS : positions.data() + i * POSITION_COMPONENTS, UV_COMPONENTS, out);

					// With colours and no normals, aNormal reads zeros, as it does
					// from vertices drawn without normals.
					if (hasNormals)
						out = std::copy_n(normals.data() + i * NORMAL_COMPONENTS, NORMAL_COMPONENTS, out);
					else if (hasColors)
						out = std::copy_n(NO_NORMAL, NORMAL_COMPONENTS, out);

					if (hasColors)
						out = std::copy_n(colors.data() + i * COLOR_COMPONENTS, COLOR_COMPONENTS, out);
				};

				renderer.SetVertices(m_kept, interleaved.data(), count, *std::as_const(m_draw_type), format);
				m_geometry = batchable ? Hashed(interleaved, format) : 0;
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

		// Each mesh lays down its depth before its colour, so only its nearest
		// surface blends over what is behind it, and one with opaque colours
		// draws just as it would with transparency off.
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
			};

			ready(first, lot.instanced);
			renderer->DrawInstances(lot.instanced, *std::as_const(first.primitive_id), first.m_kept, m_instances.data(), lot.meshes.size());
		};

		m_lots.clear();
		m_byKey.clear();
	};

	void Mesh::OnGui()
	{
		ImGui::Text("Vertices: %zu", std::as_const(m_vertices)->size());
		ImGui::Text("Indices:  %zu", m_indices.size());
		ImGui::Text("UVs:      %zu", std::as_const(m_textureCoordinates)->size());
		ImGui::Text("Normals:  %zu", std::as_const(m_normals)->size());
		ImGui::Text("Colours:  %zu", std::as_const(m_colors)->size());

		if (m_generated)
			ImGui::TextDisabled("Generated by a script: not saved");

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
};
