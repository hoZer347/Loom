#include "SelectionOutline.h"

#include "Camera.h"
#include "GameObject.h"
#include "Mesh.h"
#include "OpenGL.h"
#include "Renderer.h"
#include "Shaders.h"
#include "Sprite.h"

#include <cstring>
#include <iostream>
#include <iterator>
#include <sstream>
#include <typeinfo>


namespace Loom
{
	namespace
	{
		constexpr glm::vec4 outline_colour(1.0f, 0.55f, 0.1f, 1.0f);

		constexpr float uncovered[] = { 0.0f, 0.0f, 0.0f, 0.0f };

		// Two triangles over the whole target, in clip space.
		constexpr float screen[] =
		{
			-1.0f, -1.0f, 0.0f,
			 1.0f, -1.0f, 0.0f,
			 1.0f,  1.0f, 0.0f,
			-1.0f, -1.0f, 0.0f,
			 1.0f,  1.0f, 0.0f,
			-1.0f,  1.0f, 0.0f,
		};
		constexpr size_t floats_per_vertex = 3;
		constexpr size_t screen_vertices = std::size(screen) / floats_per_vertex;

		const char* const mesh_mask_shader = R"(
// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

void main()
{
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}

// ===FRAGMENT===

out vec4 FragColor;

void main()
{
	FragColor = vec4(1.0);
}
)";

		// Cut out the way Sprite cuts its quad, so the ring follows the image.
		const char* const sprite_mask_shader = R"(
// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

out vec2 v_uv;

void main()
{
	v_uv = aPos.xy;
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}

// ===FRAGMENT===

in vec2 v_uv;

uniform sampler2D u_texture;
uniform vec4 u_color;
uniform float u_alphaCutoff;

out vec4 FragColor;

void main()
{
	if (texture(u_texture, v_uv).a * u_color.a < u_alphaCutoff)
		discard;

	FragColor = vec4(1.0);
}
)";

		// The mask is the size of the target, so a fragment's own pixel is the
		// mask's.
		const char* const edge_shader = R"(
// ===VERTEX===

layout(location = 0) in vec3 aPos;

void main()
{
	gl_Position = vec4(aPos, 1.0);
}

// ===FRAGMENT===

uniform sampler2D u_mask;
uniform vec4 u_color;

out vec4 FragColor;

const int RADIUS = 2;
const float COVERED = 0.5;

void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	ivec2 last = textureSize(u_mask, 0) - 1;

	if (texelFetch(u_mask, pixel, 0).r > COVERED)
		discard;

	for (int x = -RADIUS; x <= RADIUS; x++)
		for (int y = -RADIUS; y <= RADIUS; y++)
			if (texelFetch(u_mask, clamp(pixel + ivec2(x, y), ivec2(0), last), 0).r > COVERED)
			{
				FragColor = u_color;
				return;
			}

	discard;
}
)";

		// Like the materials' shaders, they live until the process ends. 0 when
		// one will not compile, which is reported once.
		uint32_t Compile(const char* name, const char* source)
		{
			try
			{
				std::istringstream stream(source);
				return (new Shader(name, stream))->id;
			}
			catch (const std::exception& e)
			{
				std::cerr << "Selection outline: " << e.what() << std::endl;
				return 0;
			};
		};

		uint32_t MeshMaskProgram()
		{
			static const uint32_t program = Compile("Loom Editor Mesh Mask", mesh_mask_shader);
			return program;
		};

		uint32_t SpriteMaskProgram()
		{
			static const uint32_t program = Compile("Loom Editor Sprite Mask", sprite_mask_shader);
			return program;
		};

		uint32_t EdgeProgram()
		{
			static const uint32_t program = Compile("Loom Editor Selection Outline", edge_shader);
			return program;
		};

		void DrawSilhouettes(GameObject& gameObject)
		{
			// By type name, the way GetComponent matches: a script library's Mesh
			// carries its own copy of the type info.
			for (ComponentBase* component : gameObject.GetComponents())
				if (strcmp(component->GetTypeName(), typeid(Mesh).name()) == 0)
				{
					Mesh* mesh = (Mesh*)component;

					if (mesh->IsDrawable())
						mesh->Draw(MeshMaskProgram());
				}
				else if (strcmp(component->GetTypeName(), typeid(Sprite).name()) == 0)
					((Sprite*)component)->DrawQuad(SpriteMaskProgram());

			for (GameObject* child : gameObject.GetChildren())
				DrawSilhouettes(*child);
		};
	};

	void SelectionOutline::Draw(GameObject& selected, const Camera& camera)
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer || !MeshMaskProgram() || !SpriteMaskProgram() || !EdgeProgram())
			return;

		const glm::ivec2 size = renderer->TargetSize();

		m_mask.Render(
			size.x,
			size.y,
			uncovered,
			[&]()
			{
				camera.Apply(MeshMaskProgram());
				camera.Apply(SpriteMaskProgram());

				DrawSilhouettes(selected);
			});

		if (!m_mask.GetTexture())
			return;

		// Otherwise the scene's depth would hide the ring wherever it overlaps
		// something nearer than the screen quad.
		renderer->Clear(nullptr, true);

		renderer->SetTexture(EdgeProgram(), "u_mask", m_mask.GetTexture());
		renderer->SetUniform(EdgeProgram(), "u_color", outline_colour);
		renderer->Draw(EdgeProgram(), GL_TRIANGLES, screen, screen_vertices, GL_STATIC_DRAW);
	};
};
