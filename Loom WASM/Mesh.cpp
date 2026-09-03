#include "Mesh.h"

#include "Engine.h"
#include "OpenGL.h"
#include "Shaders.h"
#include "Material.h"
#include "GameObject.h"

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

	void Mesh::OnRender()
	{
		if (material == nullptr)
			material = m_gameObject->GetComponent<Material>();

		// A mesh attached without a material (or before one is set up, which is
		// the normal state for a mesh just added in the editor) has nothing to
		// draw with; skip it instead of dereferencing null.
		if (material == nullptr || material->shader == nullptr)
			return;

		glUseProgram(material->shader->id);

		glBindBuffer(GL_ARRAY_BUFFER, Engine::VBO);
		glBufferData(GL_ARRAY_BUFFER, m_vertices->size() * sizeof(float), m_vertices->data(), m_draw_type);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(0);

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

			glDisableVertexAttribArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			return;
		}
		// m_vertices holds floats, three per vertex, but glDrawArrays counts
		// vertices. Passing the float count asks GL to read past the end of the
		// buffer, which WebGL rejects outright (INVALID_OPERATION, nothing drawn).
		else glDrawArrays(primitive_id, 0, (GLsizei)(m_vertices->size() / 3));

		glDisableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
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
