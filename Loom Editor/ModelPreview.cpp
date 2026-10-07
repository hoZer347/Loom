#include "ModelPreview.h"

#include "EditorTheme.h"
#include "ModelImporter.h"

#include "OpenGL.h"
#include "Renderer.h"
#include "Shaders.h"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <sstream>


namespace Loom
{
	namespace
	{
		constexpr float radians_per_degree = std::numbers::pi_v<float> / 180.0f;

		constexpr float field_of_view = 30.0f * radians_per_degree;

		// How far above the turntable the camera looks down from.
		constexpr float elevation = 25.0f * radians_per_degree;

		// Room around the model's bounding sphere, so it never touches the edge.
		constexpr float margin = 1.15f;

		const char* const preview_shader = R"(
// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

out vec3 v_world;

void main()
{
	vec4 world = u_model * vec4(aPos, 1.0);
	v_world = world.xyz;
	gl_Position = u_viewProjection * world;
}

// ===FRAGMENT===

in vec3 v_world;

uniform vec3 u_color;

out vec4 FragColor;

const vec3 light = normalize(vec3(0.4, 0.8, 0.6));
const float ambient = 0.25;

void main()
{
	// Flat shading from the slope of the face, lit from either side so a
	// model's winding does not matter.
	vec3 normal = normalize(cross(dFdx(v_world), dFdy(v_world)));
	float lit = ambient + (1.0 - ambient) * abs(dot(normal, light));

	FragColor = vec4(u_color * lit, 1.0);
}
)";

		// Like the materials' shaders, it lives until the process ends.
		uint32_t PreviewProgram()
		{
			static const uint32_t program = []() -> uint32_t
			{
				try
				{
					std::istringstream source(preview_shader);
					return (new Shader("Loom Editor Model Preview", source))->id;
				}
				catch (const std::exception& e)
				{
					std::cerr << "Model preview: " << e.what() << std::endl;
					return 0;
				};
			}();

			return program;
		};
	};

	size_t ModelPreview::GetTriangleCount() const
	{
		return m_vertices.size() / model_floats_per_vertex / model_vertices_per_triangle;
	};

	void ModelPreview::Show(const std::string& path)
	{
		std::error_code code;
		const std::filesystem::file_time_type write_time = std::filesystem::last_write_time(path, code);

		if (path == m_path && write_time == m_writeTime)
			return;

		m_path = path;
		m_writeTime = write_time;
		m_vertices.clear();

		const std::vector<ModelPart> parts = LoadModel(path);

		for (const ModelPart& part : parts)
			m_vertices.insert(m_vertices.end(), part.vertices.begin(), part.vertices.end());

		m_meshCount = parts.size();

		float furthest = 0.0f;

		for (size_t i = 0; i < m_vertices.size(); i += model_floats_per_vertex)
			furthest = std::max(furthest, glm::length(glm::make_vec3(&m_vertices[i])));

		m_radius = furthest > 0.0f ? furthest : model_fit_extent;
	};

	void ModelPreview::Render(int size, float angle, const float* clear_colour)
	{
		Renderer* renderer = Renderer::Get();
		const uint32_t program = PreviewProgram();

		if (renderer == nullptr || program == 0 || !IsLoaded())
			return;

		m_target.Render(
			size,
			size,
			clear_colour,
			[this, renderer, program, angle]()
			{
				const float reach = m_radius * margin;
				const float distance = reach / std::sin(field_of_view / 2.0f);

				const glm::vec3 eye = distance * glm::vec3(0.0f, std::sin(elevation), std::cos(elevation));
				const glm::vec3 up(0.0f, 1.0f, 0.0f);

				const glm::mat4 view_projection =
					glm::perspective(field_of_view, 1.0f, distance - reach, distance + reach) *
					glm::lookAt(eye, glm::vec3(0.0f), up);

				const ImVec4& colour = EditorTheme::AccentText;

				renderer->SetUniform(program, "u_model", glm::rotate(glm::mat4(1.0f), angle, up));
				renderer->SetUniform(program, "u_viewProjection", view_projection);
				renderer->SetUniform(program, "u_color", glm::vec3(colour.x, colour.y, colour.z));
				renderer->Draw(
					program,
					GL_TRIANGLES,
					m_vertices.data(),
					m_vertices.size() / model_floats_per_vertex,
					GL_STATIC_DRAW);
			});
	};
};
