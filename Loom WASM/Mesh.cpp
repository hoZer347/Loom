#include "Mesh.h"

#include "Camera.h"
#include "Engine.h"
#include "OpenGL.h"
#include "Renderer.h"
#include "Shaders.h"
#include "Material.h"
#include "GameObject.h"
#include "Light.h"
#include "Skin.h"
#include "Utilities/Clock.h"

#include <algorithm>
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

	Mesh::Mesh(Primitive primitive)
	{
		primitive_id = primitive;
	};

	Mesh::~Mesh()
	{ };

	bool Mesh::IsSaved(const SerializedField& field) const
	{
		if (!*m_generated)
			return true;

		return field.data != &*m_vertices && field.data != &*m_textureCoordinates && field.data != &*m_normals;
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

		material->Apply(material->shader->id, &*variables);

		if (Camera::current)
			Camera::current->Apply(material->shader->id);
		// Without a camera, world space is what the screen shows.
		else if (Renderer* renderer = Renderer::Get())
			renderer->SetUniform(material->shader->id, "u_viewProjection", glm::mat4(1.0f));

		if (Light::current)
			Light::current->Apply(material->shader->id);
		else Light::ApplyNone(material->shader->id);

		Draw(material->shader->id);
	};

	void Mesh::Draw(uint32_t program)
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer)
			return;

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

			return;
		};

		constexpr size_t POSITION_COMPONENTS = 3;
		constexpr size_t UV_COMPONENTS = 2;
		constexpr size_t NORMAL_COMPONENTS = 3;

		if (Skin* skin = m_gameObject->GetComponent<Skin>())
		{
			if (m_deformed.size() != m_vertices->size() || m_deformedFrame != Time::FrameCount())
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

		const std::vector<float>& positions = m_deformed.size() == m_vertices->size() ? m_deformed : *m_vertices;
		const std::vector<float>& normals = m_deformedNormals.size() == m_normals->size() ? m_deformedNormals : *m_normals;
		const size_t count = positions.size() / POSITION_COMPONENTS;
		const bool hasUVs = m_textureCoordinates->size() == count * UV_COMPONENTS;
		const bool hasNormals = normals.size() == count * NORMAL_COMPONENTS;

		renderer->SetUniform(program, "u_model", m_gameObject->WorldMatrix());

		if (!hasUVs && !hasNormals)
		{
			renderer->Draw(program, primitive_id, positions.data(), count, m_draw_type);
			return;
		};

		const VertexFormat format = hasNormals ? VertexFormat::PositionUVNormal : VertexFormat::PositionUV;
		m_interleaved.resize(count * FloatsPerVertex(format));
		float* out = m_interleaved.data();

		for (size_t i = 0; i < count; i++)
		{
			// Without texture coordinates, aUV reads the position's x and y, as
			// it does from vertices drawn without normals.
			out = std::copy_n(positions.data() + i * POSITION_COMPONENTS, POSITION_COMPONENTS, out);
			out = std::copy_n(hasUVs ? m_textureCoordinates->data() + i * UV_COMPONENTS : positions.data() + i * POSITION_COMPONENTS, UV_COMPONENTS, out);

			if (hasNormals)
				out = std::copy_n(normals.data() + i * NORMAL_COMPONENTS, NORMAL_COMPONENTS, out);
		};

		renderer->Draw(program, primitive_id, m_interleaved.data(), count, m_draw_type, format);
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

	void Mesh::OnGui()
	{
		ImGui::Text("Vertices: %zu", m_vertices->size());
		ImGui::Text("Indices:  %zu", m_indices.size());
		ImGui::Text("UVs:      %zu", m_textureCoordinates->size());
		ImGui::Text("Normals:  %zu", m_normals->size());

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
		DrawShaderVariables(declared, UniformScope::Instance, variables);
	};
};
