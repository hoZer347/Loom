#include "Sprite.h"

#include "Camera.h"
#include "GameObject.h"
#include "Light.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Shaders.h"
#include "Textures.h"

#include "glm/gtc/matrix_transform.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>


namespace Loom
{
	namespace
	{
		constexpr uint32_t STATIC_DRAW = 0x88E4;	// GL_STATIC_DRAW

		// Two triangles over the unit square. The vertex shader takes its
		// texture coordinates straight off the positions.
		constexpr float QUAD[] =
		{
			0.0f, 0.0f, 0.0f,
			1.0f, 0.0f, 0.0f,
			1.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f,
			1.0f, 1.0f, 0.0f,
			0.0f, 1.0f, 0.0f,
		};
		constexpr size_t FLOATS_PER_VERTEX = 3;
		constexpr size_t QUAD_VERTICES = std::size(QUAD) / FLOATS_PER_VERTEX;

		// Keeps a zero or negative pixelsPerUnit from making the quad infinite or
		// turning it inside out.
		constexpr float MIN_PIXELS_PER_UNIT = 1e-3f;

		const char* const SPRITE_SHADER = R"(
// ===COMMON===

precision highp float;
precision highp sampler2DShadow;

// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_viewProjection;
uniform mat4 u_lightViewProjection;
uniform mat4 u_model;

out vec2 v_uv;
out vec3 v_world;
out vec4 v_lightSpace;

void main()
{
	vec4 world = u_model * vec4(aPos, 1.0);

	v_uv = aPos.xy;
	v_world = world.xyz;
	v_lightSpace = u_lightViewProjection * world;
	gl_Position = u_viewProjection * world;
}

// ===FRAGMENT===

in vec2 v_uv;
in vec3 v_world;
in vec4 v_lightSpace;

uniform sampler2D u_texture;
uniform vec4 u_color;
uniform float u_alphaCutoff;

uniform vec3 u_lightDirection;
uniform vec3 u_lightColor;
uniform float u_ambient;
uniform sampler2DShadow u_shadowMap;

out vec4 FragColor;

const int PCF_RADIUS = 1;

// Clip space runs -1 to 1; the shadow map's texture and depth coordinates run
// 0 to 1.
const float CLIP_TO_TEXTURE = 0.5;

float Visibility()
{
	vec3 coords = v_lightSpace.xyz / v_lightSpace.w * CLIP_TO_TEXTURE + CLIP_TO_TEXTURE;

	if (any(lessThan(coords, vec3(0.0))) || any(greaterThan(coords, vec3(1.0))))
		return 1.0;

	vec2 texel = 1.0 / vec2(textureSize(u_shadowMap, 0));

	float lit = 0.0;
	float taps = 0.0;

	for (int x = -PCF_RADIUS; x <= PCF_RADIUS; x++)
		for (int y = -PCF_RADIUS; y <= PCF_RADIUS; y++)
		{
			lit += texture(u_shadowMap, vec3(coords.xy + vec2(x, y) * texel, coords.z));
			taps += 1.0;
		}

	return lit / taps;
}

void main()
{
	vec4 texel = texture(u_texture, v_uv) * u_color;

	if (texel.a < u_alphaCutoff)
		discard;

	// A card is lit from whichever side the light is on.
	vec3 normal = normalize(cross(dFdx(v_world), dFdy(v_world)));
	float diffuse = abs(dot(normal, u_lightDirection));

	vec3 color = texel.rgb * (u_ambient + diffuse * Visibility() * u_lightColor);

	FragColor = vec4(color, texel.a);
}
)";

		// The shadow pass: depth only, cut out the same way as above.
		const char* const SPRITE_DEPTH_SHADER = R"(
// ===COMMON===

precision highp float;

// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_lightViewProjection;
uniform mat4 u_model;

out vec2 v_uv;

void main()
{
	v_uv = aPos.xy;
	gl_Position = u_lightViewProjection * u_model * vec4(aPos, 1.0);
}

// ===FRAGMENT===

in vec2 v_uv;

uniform sampler2D u_texture;
uniform vec4 u_color;
uniform float u_alphaCutoff;

void main()
{
	if (texture(u_texture, v_uv).a * u_color.a < u_alphaCutoff)
		discard;
}
)";

		// Shared by every sprite and kept until the process ends, like the
		// materials' shaders. 0 when it will not compile, which is reported once.
		uint32_t Compile(const char* name, const char* source)
		{
			try
			{
				std::istringstream stream(source);
				return (new Shader(name, stream))->id;
			}
			catch (const std::exception& e)
			{
				std::cerr << "Sprite: " << e.what() << std::endl;
				return 0;
			};
		};

		uint32_t SpriteProgram()
		{
			static const uint32_t program = Compile("Loom Sprite", SPRITE_SHADER);
			return program;
		};

		uint32_t SpriteDepthProgram()
		{
			static const uint32_t program = Compile("Loom Sprite Depth", SPRITE_DEPTH_SHADER);
			return program;
		};
	};

	Texture* Sprite::GetTexture()
	{
		if (*texturePath != m_loaded_path)
		{
			m_loaded_path = texturePath;
			m_texture = m_loaded_path.empty() ? nullptr : Texture::Shared(m_loaded_path);
		};

		return m_texture;
	};

	glm::mat4 Sprite::QuadMatrix()
	{
		const Texture* texture = GetTexture();
		const glm::vec2 pixels = texture ? glm::vec2(texture->width, texture->height) : glm::vec2(0.0f);
		const glm::vec2 size = pixels / std::max((float)pixelsPerUnit, MIN_PIXELS_PER_UNIT);

		const glm::vec2& origin = pivot;

		glm::mat4 quad = glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
		quad = glm::translate(quad, glm::vec3(-origin, 0.0f));

		return m_gameObject->WorldMatrix() * quad;
	};

	void Sprite::OnRender()
	{
		const uint32_t program = SpriteProgram();

		if (!program)
			return;

		if (Camera::current)
			Camera::current->Apply(program);

		if (Light::current)
			Light::current->Apply(program);
		else Light::ApplyNone(program);

		DrawQuad(program);
	};

	void Sprite::DrawShadow(const glm::mat4& lightViewProjection)
	{
		const uint32_t program = SpriteDepthProgram();

		if (!castShadows || !program)
			return;

		if (Renderer* renderer = Renderer::Get())
			renderer->SetUniform(program, "u_lightViewProjection", lightViewProjection);

		DrawQuad(program);
	};

	void Sprite::DrawQuad(uint32_t program)
	{
		Renderer* renderer = Renderer::Get();
		const Texture* texture = GetTexture();

		if (!renderer || !texture || !texture->handle)
			return;

		renderer->SetTexture(program, "u_texture", texture->handle);
		renderer->SetUniform(program, "u_color", *color);
		renderer->SetUniform(program, "u_alphaCutoff", (float)alphaCutoff);
		renderer->SetUniform(program, "u_model", QuadMatrix());

		renderer->Draw(program, Mesh::triangles, QUAD, QUAD_VERTICES, STATIC_DRAW);
	};
};
