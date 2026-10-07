#include "Mesh.h"

#include "Camera.h"
#include "Engine.h"
#include "OpenGL.h"
#include "Renderer.h"
#include "Shaders.h"
#include "Material.h"
#include "GameObject.h"
#include "Light.h"

#include <iostream>


namespace Loom
{
	namespace
	{
		enum DrawType
		{
			STATIC = GL_STATIC_DRAW,
			DYNAMIC = GL_DYNAMIC_DRAW,
			STREAM = GL_STREAM_DRAW,
		};

		const char* DrawTypeName(uint32_t draw_type)
		{
			switch (draw_type)
			{
			case STATIC:	return "STATIC";
			case STREAM:	return "STREAM";
			default:	return "DYNAMIC";
			};
		};

		uint32_t NextDrawType(uint32_t draw_type)
		{
			switch (draw_type)
			{
			case DYNAMIC:	return STATIC;
			case STATIC:	return STREAM;
			default:	return DYNAMIC;
			};
		};
	};

	Mesh::Mesh(uint32_t primitive)
	{
		primitive_id = primitive;
	};

	Mesh::~Mesh()
	{ };

	bool Mesh::IsDrawable()
	{
		if (material == nullptr)
			material = m_gameObject->GetComponent<Material>();

		// A mesh attached without a material (or before one is set up, which is
		// the normal state for a mesh just added in the editor) has nothing to
		// draw with.
		return material != nullptr && material->shader != nullptr;
	};

	void Mesh::OnRender()
	{
		if (!IsDrawable())
			return;

		material->Apply(material->shader->id);

		if (Camera::current)
			Camera::current->Apply(material->shader->id);

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

		constexpr size_t COMPONENTS = 3;

		renderer->SetUniform(program, "u_model", m_gameObject->WorldMatrix());
		renderer->Draw(program, primitive_id, m_vertices->data(), m_vertices->size() / COMPONENTS, m_draw_type);
	};

	void Mesh::OnGui()
	{
		ImGui::Text("Vertices: %zu", m_vertices->size());
		ImGui::Text("Indices:  %zu", m_indices.size());

		// Read off the value: a label remembered beside it would be one string
		// shared by every mesh in the process.
		if (ImGui::Button(DrawTypeName(m_draw_type)))
			m_draw_type = NextDrawType(m_draw_type);
	};
};
