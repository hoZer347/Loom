#include "SelectionOutline.h"

#include "Camera.h"
#include "GameObject.h"
#include "Material.h"
#include "Mesh.h"
#include "OpenGL.h"
#include "Renderer.h"
#include "Shaders.h"
#include "Sprite.h"

#include <cstring>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <typeinfo>
#include <utility>


namespace Loom
{
	namespace
	{
		constexpr glm::vec4 outline_colour(1.0f, 0.55f, 0.1f, 1.0f);

		constexpr const char* mesh_mask_name = "Loom Editor Mesh Mask";

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

		const char* const mesh_mask_vertex = R"(
layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

void main()
{
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}
)";

		const char* const mesh_mask_fragment = R"(
out vec4 FragColor;

void main()
{
	FragColor = vec4(1.0);
}
)";

		const char* const sprite_mask_vertex = R"(
layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

out vec2 v_uv;

void main()
{
	v_uv = aPos.xy;
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}
)";

		// Cut out the way Sprite cuts its quad, so the ring follows the image.
		const char* const sprite_mask_fragment = R"(
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

		const char* const edge_vertex = R"(
layout(location = 0) in vec3 aPos;

void main()
{
	gl_Position = vec4(aPos, 1.0);
}
)";

		// The mask is the size of the target, so a fragment's own pixel is the
		// mask's. The distance to the silhouette's edge, negative inside it,
		// places the ring: WIDTH pixels wide, fading in over the edge itself and
		// out beyond, each over SOFTNESS, so neither side follows the pixels.
		const char* const edge_fragment = R"(
uniform sampler2D u_mask;
uniform vec4 u_color;

out vec4 FragColor;

const float WIDTH = 2.0;
const float SOFTNESS = 1.0;

// Halfway between a pixel and its neighbour is where the silhouette's edge lies.
const float HALF_PIXEL = 0.5;

const int REACH = int(ceil(WIDTH + SOFTNESS + HALF_PIXEL));
const float COVERED = 0.5;

bool Covered(ivec2 pixel, ivec2 last)
{
	return texelFetch(u_mask, clamp(pixel, ivec2(0), last), 0).r > COVERED;
}

void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	ivec2 last = textureSize(u_mask, 0) - 1;

	bool inside = Covered(pixel, last);
	float nearest = float(REACH);

	for (int x = -REACH; x <= REACH; x++)
		for (int y = -REACH; y <= REACH; y++)
			if (Covered(pixel + ivec2(x, y), last) != inside)
				nearest = min(nearest, length(vec2(x, y)));

	float edge = nearest - HALF_PIXEL;
	float from_edge = inside ? -edge : edge;

	float alpha =
		clamp((from_edge + SOFTNESS) / SOFTNESS, 0.0, 1.0) *
		clamp((WIDTH + SOFTNESS - from_edge) / SOFTNESS, 0.0, 1.0);

	if (alpha <= 0.0)
		discard;

	FragColor = vec4(u_color.rgb, u_color.a * alpha);
}
)";
	};

	SelectionOutline::~SelectionOutline()
	{
		Destroy();
	};

	bool SelectionOutline::Compile()
	{
		if (m_compileTried)
			return m_edge != 0;

		m_compileTried = true;

		Renderer& renderer = *Renderer::Get();

		try
		{
			m_meshMask = renderer.CreateProgram(mesh_mask_name, mesh_mask_vertex, mesh_mask_fragment);
			m_spriteMask = renderer.CreateProgram("Loom Editor Sprite Mask", sprite_mask_vertex, sprite_mask_fragment);
			m_edge = renderer.CreateProgram("Loom Editor Selection Outline", edge_vertex, edge_fragment);
		}
		catch (const std::exception& e)
		{
			std::cerr << "Selection outline: " << e.what() << std::endl;
			Destroy();
		};

		return m_edge != 0;
	};

	void SelectionOutline::Destroy()
	{
		Renderer* renderer = Renderer::Get();

		for (const auto& [shader, mask] : m_meshMasks)
			if (renderer && mask.program != m_meshMask)
				renderer->DeleteProgram(mask.program);

		m_meshMasks.clear();

		for (uint32_t* program : { &m_meshMask, &m_spriteMask, &m_edge })
		{
			if (renderer && *program)
				renderer->DeleteProgram(*program);

			*program = 0;
		};
	};

	uint32_t SelectionOutline::MeshMask(const Shader& shader)
	{
		ShaderMask& mask = m_meshMasks[&shader];

		if (mask.program && mask.shader == shader.id)
			return mask.program;

		Renderer& renderer = *Renderer::Get();

		if (mask.program && mask.program != m_meshMask)
			renderer.DeleteProgram(mask.program);

		mask.shader = shader.id;

		try
		{
			mask.program = renderer.CreateProgram(std::string(mesh_mask_name) + ": " + shader.file_path, shader.vertex, mesh_mask_fragment);
		}
		catch (const std::exception& e)
		{
			std::cerr << "Selection outline: " << e.what() << std::endl;
			mask.program = m_meshMask;
		};

		return mask.program;
	};

	bool SelectionOutline::DrawSilhouettes(GameObject& gameObject, const Camera& camera)
	{
		bool drew = false;

		// By type name, the way GetComponent matches: a script library's Mesh
		// carries its own copy of the type info.
		for (ComponentBase* component : gameObject.GetComponents())
			if (strcmp(component->GetTypeName(), typeid(Mesh).name()) == 0)
			{
				Mesh* mesh = (Mesh*)component;

				if (!mesh->IsDrawable())
					continue;

				const uint32_t program = MeshMask(*mesh->material->shader);

				mesh->material->Apply(program, &*std::as_const(mesh->variables));
				camera.Apply(program);
				mesh->Draw(program);
				drew = true;
			}
			else if (strcmp(component->GetTypeName(), typeid(Sprite).name()) == 0)
				drew |= ((Sprite*)component)->DrawQuad(m_spriteMask);

		for (GameObject* child : gameObject.GetChildren())
			drew |= DrawSilhouettes(*child, camera);

		return drew;
	};

	void SelectionOutline::Draw(GameObject& selected, const Camera& camera)
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer || !Compile())
			return;

		const glm::ivec2 size = renderer->TargetSize();
		bool drew = false;

		m_mask.Render(
			size.x,
			size.y,
			uncovered,
			[&]()
			{
				// The mesh program last, so that when nothing draws it is the one
				// left bound: GL calls the sprite program's sampler undefined
				// behaviour while it would read the shadow map left on its unit.
				camera.Apply(m_spriteMask);
				camera.Apply(m_meshMask);

				drew = DrawSilhouettes(selected, camera);
			});

		if (!drew)
			return;

		// Otherwise the scene's depth would hide the ring wherever it overlaps
		// something nearer than the screen quad.
		renderer->Clear(nullptr, true);

		renderer->SetTexture(m_edge, "u_mask", m_mask.GetTexture());
		renderer->SetUniform(m_edge, "u_color", outline_colour);
		renderer->Draw(m_edge, GL_TRIANGLES, screen, screen_vertices, GL_STATIC_DRAW);
	};
};
