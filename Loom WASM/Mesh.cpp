#include "Mesh.h"

#include "Engine.h"
#include "Shaders.h"
#include "Material.h"
#include "GameObject.h"


namespace Loom
{
	void Mesh::OnRender()
	{
		if (material == nullptr)
			material = m_gameObject->GetComponent<Material>();

		glUseProgram(material->m_shader->m_id);
		
		glBindBuffer(GL_ARRAY_BUFFER, Engine::VBO);
		glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(float), m_vertices.data(), GL_DYNAMIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Engine::EBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(uint32_t), m_indices.data(), GL_DYNAMIC_DRAW);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(0);

		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

		glDisableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	};
};
