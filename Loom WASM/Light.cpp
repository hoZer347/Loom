#include "Light.h"

#include "GameObject.h"
#include "Mesh.h"
#include "RenderMath.h"
#include "Renderer.h"
#include "Shaders.h"
#include "Sprite.h"

#include "glm/gtc/matrix_transform.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <typeinfo>


namespace Loom
{
	namespace
	{
		// Pushes the casters' depth back by their slope, which is what keeps a
		// lit surface from shadowing itself in stripes (shadow acne).
		constexpr float SLOPE_OFFSET = 2.0f;
		constexpr float CONSTANT_OFFSET = 4.0f;

		// The light sits one extent back from the centre, so the box is two deep.
		constexpr float DEPTH_EXTENTS = 2.0f;

		constexpr int MIN_RESOLUTION = 1;

		// With no light to shade by, a material shows its colour as it is.
		constexpr float UNLIT_AMBIENT = 1.0f;

		const char* const DEPTH_SHADER = R"(
// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_model;

void main()
{
	gl_Position = LoomLightSpace(u_model * vec4(aPos, 1.0));
}

// ===FRAGMENT===

void main()
{
}
)";

		const char* const VERTEX_LIBRARY = R"(
uniform mat4 u_lightViewProjection;

vec4 LoomLightSpace(vec4 world)
{
	return u_lightViewProjection * world;
}
)";

		const char* const FRAGMENT_LIBRARY = R"(
uniform vec3 u_lightDirection;
uniform vec3 u_lightColor;
uniform float u_ambient;
uniform highp sampler2DShadow u_shadowMap;

// Taps either side of the centre texel, on top of the 2x2 the hardware
// comparison already filters.
const int LOOM_PCF_RADIUS = 1;

// Clip space runs -1 to 1; the shadow map's texture and depth coordinates run
// 0 to 1.
const float LOOM_CLIP_TO_TEXTURE = 0.5;

float LoomShadow(vec4 lightSpace)
{
	vec3 coords = lightSpace.xyz / lightSpace.w * LOOM_CLIP_TO_TEXTURE + LOOM_CLIP_TO_TEXTURE;

	// Outside the shadow map is outside what the light can say anything about.
	if (any(lessThan(coords, vec3(0.0))) || any(greaterThan(coords, vec3(1.0))))
		return 1.0;

	vec2 texel = 1.0 / vec2(textureSize(u_shadowMap, 0));

	float lit = 0.0;
	float taps = 0.0;

	for (int x = -LOOM_PCF_RADIUS; x <= LOOM_PCF_RADIUS; x++)
		for (int y = -LOOM_PCF_RADIUS; y <= LOOM_PCF_RADIUS; y++)
		{
			lit += texture(u_shadowMap, vec3(coords.xy + vec2(x, y) * texel, coords.z));
			taps += 1.0;
		}

	return lit / taps;
}

vec3 LoomLight(vec3 normal, vec4 lightSpace)
{
	float diffuse = max(dot(normal, -u_lightDirection), 0.0);

	return vec3(u_ambient) + diffuse * LoomShadow(lightSpace) * u_lightColor;
}
)";

		bool IsIdentifier(char c)
		{
			return isalnum((unsigned char)c) || c == '_';
		};

		// A stage uses a library when it calls one of its functions: the whole
		// name, then the parenthesis, with any whitespace between.
		bool Calls(const std::string& source, std::initializer_list<const char*> functions)
		{
			for (const char* function : functions)
			{
				const size_t length = strlen(function);

				for (size_t at = source.find(function); at != std::string::npos; at = source.find(function, at + length))
				{
					if (at > 0 && IsIdentifier(source[at - 1]))
						continue;

					const size_t next = source.find_first_not_of(" \t\r\n", at + length);

					if (next != std::string::npos && source[next] == '(')
						return true;
				};
			};

			return false;
		};

		// One program shared by every light. Like the materials' shaders, it
		// lives until the process ends.
		uint32_t DepthProgram()
		{
			static const uint32_t program = []() -> uint32_t
			{
				try
				{
					std::istringstream source(DEPTH_SHADER);
					return (new Shader("Loom Shadow Depth", source))->id;
				}
				catch (const std::exception& e)
				{
					std::cerr << "Light: " << e.what() << std::endl;
					return 0;
				};
			}();

			return program;
		};

		// A one-texel map at the far plane: nothing is in shadow. Bound in place
		// of a light's own map when there is none, so a shadow sampler always
		// has a depth texture behind it. Shared, and lives until the process ends.
		uint32_t NothingInShadow(Renderer& renderer)
		{
			static const uint32_t texture = [&renderer]() -> uint32_t
			{
				const uint32_t created = renderer.CreateDepthTarget(1);

				if (created)
				{
					renderer.PushTarget(created);
					renderer.Clear(nullptr, true);
					renderer.PopTarget();
				};

				return created;
			}();

			return texture;
		};

		// A zero vector has no direction to normalise; straight down is the
		// least surprising stand-in.
		glm::vec3 Normalised(const glm::vec3& direction)
		{
			return glm::length(direction) > 0.0f
				? glm::normalize(direction)
				: glm::vec3(0.0f, -1.0f, 0.0f);
		};
	};

	std::string Light::VertexLibrary(const std::string& source)
	{
		return Calls(source, { "LoomLightSpace" }) ? VERTEX_LIBRARY : "";
	};

	std::string Light::FragmentLibrary(const std::string& source)
	{
		return Calls(source, { "LoomShadow", "LoomLight" }) ? FRAGMENT_LIBRARY : "";
	};

	Light::~Light()
	{
		Release();
	};

	glm::mat4 Light::ViewProjection() const
	{
		const glm::vec3 forward = Normalised(direction);
		const glm::vec3 center = *shadowCenter;
		const float extent = shadowExtent;

		const glm::mat4 view = glm::lookAt(center - forward * extent, center, UpFor(forward));
		const glm::mat4 projection = glm::ortho(-extent, extent, -extent, extent, 0.0f, DEPTH_EXTENTS * extent);

		return projection * view;
	};

	void Light::RenderShadowMap(GameObject& root)
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer || !Allocate())
			return;

		m_view_projection = ViewProjection();

		renderer->PushTarget(m_depth_texture);
		renderer->Clear(nullptr, true);

		const uint32_t program = DepthProgram();

		if (castShadows && program)
		{
			renderer->SetUniform(program, "u_lightViewProjection", m_view_projection);
			renderer->SetDepthBias(SLOPE_OFFSET, CONSTANT_OFFSET);

			DrawCasters(root, program);

			renderer->SetDepthBias(0.0f, 0.0f);
		};

		renderer->PopTarget();
	};

	void Light::Apply(uint32_t program) const
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer)
			return;

		// A map that failed to allocate still needs something behind the sampler.
		renderer->SetTexture(program, "u_shadowMap", m_depth_texture ? m_depth_texture : NothingInShadow(*renderer));

		renderer->SetUniform(program, "u_lightDirection", Normalised(direction));
		renderer->SetUniform(program, "u_lightColor", *color * (float)intensity);
		renderer->SetUniform(program, "u_ambient", (float)ambient);
		renderer->SetUniform(program, "u_lightViewProjection", m_view_projection);
	};

	void Light::ApplyNone(uint32_t program)
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer)
			return;

		renderer->SetTexture(program, "u_shadowMap", NothingInShadow(*renderer));
		renderer->SetUniform(program, "u_lightColor", glm::vec3(0.0f));
		renderer->SetUniform(program, "u_ambient", UNLIT_AMBIENT);
	};

	bool Light::Allocate()
	{
		Renderer& renderer = *Renderer::Get();

		const int resolution = std::clamp((int)shadowResolution, MIN_RESOLUTION, std::max(MIN_RESOLUTION, renderer.MaxTextureSize()));

		if (resolution == m_allocated_resolution)
			return m_depth_texture != 0;

		Release();

		m_allocated_resolution = resolution;
		m_depth_texture = renderer.CreateDepthTarget(resolution);

		return m_depth_texture != 0;
	};

	void Light::Release()
	{
		Renderer* renderer = Renderer::Get();

		if (renderer && m_depth_texture)
			renderer->DestroyTexture(m_depth_texture);

		m_depth_texture = 0;
	};

	void Light::DrawCasters(GameObject& gameObject, uint32_t program) const
	{
		// By type name, the way GetComponent matches: a script library's Mesh
		// carries its own copy of the type info.
		for (ComponentBase* component : gameObject.GetComponents())
			if (strcmp(component->GetTypeName(), typeid(Mesh).name()) == 0)
			{
				Mesh* mesh = (Mesh*)component;

				if (mesh->IsDrawable())
					mesh->Draw(program);
			}
			else if (strcmp(component->GetTypeName(), typeid(Sprite).name()) == 0)
				((Sprite*)component)->DrawShadow(m_view_projection);

		for (GameObject* child : gameObject.GetChildren())
			DrawCasters(*child, program);
	};
};
