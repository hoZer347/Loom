#include "Light.h"

#include "GameObject.h"
#include "Material.h"
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
#include <typeinfo>
#include <utility>


namespace Loom
{
	namespace
	{
		#pragma region Shadow Map

		// Constant alone: a slope-scaled bias lights what sits just under a tilted caster.
		constexpr float CONSTANT_OFFSET = 1.0f;

		// The PCF kernel's reach, so what sits just under a caster reads it.
		constexpr float CASTER_PULL_TEXELS = 2.5f;

		// Steepest slope the pull follows; steeper shadows the caster's own lit side.
		constexpr float PULL_SLOPE_LIMIT = 2.0f;

		// The light sits one extent back from the centre, so the box is two deep.
		constexpr float DEPTH_EXTENTS = 2.0f;

		// The box reaches an extent either side of its centre across too.
		constexpr float ACROSS_EXTENTS = 2.0f;

		constexpr int MIN_RESOLUTION = 1;

		// A one-texel far-plane map bound wherever no shadow map applies, so a sampler always has one.
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

		#pragma endregion

		#pragma region Lighting

		// With no light to shade by, a material shows its colour as it is.
		constexpr float UNLIT_AMBIENT = 1.0f;

		// A zero vector falls back to straight down.
		glm::vec3 Normalised(const glm::vec3& direction)
		{
			return glm::length(direction) > 0.0f
				? glm::normalize(direction)
				: glm::vec3(0.0f, -1.0f, 0.0f);
		};

		#pragma endregion

		#pragma region Library

		const char* const LIGHT_SPACE_LIBRARY = R"(
uniform mat4 u_lightViewProjection;

// World units across one texel of the shadow map.
uniform float u_shadowTexel;

// How many shadow map texels out along its normal a surface reads its
// shadow from, clear of the flat faces it is made of, which would otherwise
// shadow the smooth surface their normals light, and past the casters' pull.
const float LOOM_NORMAL_OFFSET_TEXELS = 1.5;
// Texels more at a surface edge-on to the light, past the PCF kernel's reach.
const float LOOM_SLOPE_OFFSET_TEXELS = 5.0;

vec4 LoomLightSpace(vec4 world)
{
	return u_lightViewProjection * world;
}

// The same, read from out along a world space normal on the side facing the
// light, so a sheet lit from behind reads from in front of it all the same;
// a normal of zeros reads from the surface itself.
vec4 LoomLightSpace(vec4 world, vec3 normal)
{
	if (dot(normal, normal) == 0.0)
		return LoomLightSpace(world);

	// Depth in the shadow map grows away from the light along this.
	vec3 fromLight = normalize(vec3(u_lightViewProjection[0][2], u_lightViewProjection[1][2], u_lightViewProjection[2][2]));
	vec3 unit = normalize(normal);
	float facing = dot(unit, fromLight);
	vec3 toLight = facing > 0.0 ? -unit : unit;
	float slope = sqrt(max(1.0 - facing * facing, 0.0));

	return u_lightViewProjection * (world + vec4(toLight * u_shadowTexel * (LOOM_NORMAL_OFFSET_TEXELS + LOOM_SLOPE_OFFSET_TEXELS * slope), 0.0));
}
)";

		const char* const FRAGMENT_LIBRARY = R"(
uniform vec3 u_lightDirection;
uniform vec3 u_lightColor;
uniform float u_ambient;
uniform highp sampler2DShadow u_shadowMap;

// Set by a material whose shadows are off. Zero, which is what a program
// nothing has set it in reads, is shadowed.
uniform float u_shadowsOff;

// A 4x4 grid of taps half a texel off the texel grid, each filtered 2x2 by
// the hardware comparison. Together they weigh every texel of a 4x4 block
// equally, so a transparent caster's dither averages back to its alpha.
const int LOOM_PCF_TAPS = 4;
const float LOOM_PCF_FIRST = -1.5;

// Clip space runs -1 to 1; the shadow map's texture and depth coordinates run
// 0 to 1.
const float LOOM_CLIP_TO_TEXTURE = 0.5;

float LoomShadow(vec4 lightSpace)
{
	if (u_shadowsOff != 0.0)
		return 1.0;

	vec3 coords = lightSpace.xyz / lightSpace.w * LOOM_CLIP_TO_TEXTURE + LOOM_CLIP_TO_TEXTURE;

	// Outside the shadow map is outside what the light can say anything about.
	if (any(lessThan(coords, vec3(0.0))) || any(greaterThan(coords, vec3(1.0))))
		return 1.0;

	vec2 texel = 1.0 / vec2(textureSize(u_shadowMap, 0));

	float lit = 0.0;
	float taps = 0.0;

	for (int x = 0; x < LOOM_PCF_TAPS; x++)
		for (int y = 0; y < LOOM_PCF_TAPS; y++)
		{
			lit += texture(u_shadowMap, vec3(coords.xy + (vec2(x, y) + LOOM_PCF_FIRST) * texel, coords.z));
			taps += 1.0;
		}

	return lit / taps;
}

float LoomDiffuse(vec3 normal)
{
	return max(dot(normal, -u_lightDirection), 0.0);
}

vec3 LoomLight(vec3 normal, vec4 lightSpace)
{
	return vec3(u_ambient) + LoomDiffuse(normal) * LoomShadow(lightSpace) * u_lightColor;
}

vec3 LoomLight(vec3 normal)
{
	return vec3(u_ambient) + LoomDiffuse(normal) * u_lightColor;
}
)";

		bool IsIdentifier(char c)
		{
			return isalnum((unsigned char)c) || c == '_';
		};

		// Called is the whole name then the parenthesis, whitespace allowed between.
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

		#pragma endregion
	};

	#pragma region Shadow Map

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

	void Light::RenderShadowMap(const std::vector<GameObject*>& roots)
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer || !Allocate())
			return;

		m_view_projection = ViewProjection();

		renderer->PushTarget(m_depth_texture);
		renderer->Clear(nullptr, true);

		if (castShadows)
		{
			renderer->SetDepthBias(-CASTER_PULL_TEXELS, CONSTANT_OFFSET, -PULL_SLOPE_LIMIT * CASTER_PULL_TEXELS * ACROSS_EXTENTS / (DEPTH_EXTENTS * m_allocated_resolution));

			MeshBatch batch;

			for (GameObject* root : roots)
				DrawCasters(*root, batch);

			batch.Draw([this](Mesh& mesh, uint32_t program) { ReadyCaster(mesh, program); });

			renderer->SetDepthBias(0.0f, 0.0f, 0.0f);
			renderer->SetCullFace(CullFace::None);
		};

		renderer->PopTarget();
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

	void Light::ReadyCaster(Mesh& mesh, uint32_t program) const
	{
		Renderer& renderer = *Renderer::Get();

		// The light's view-projection stands in for the camera's.
		mesh.material->Apply(program, &*std::as_const(mesh.variables));
		// A closed mesh casts from its far side, which its lit near side then never falls behind.
		renderer.SetCullFace(mesh.FarSide());
		renderer.SetUniform(program, "u_viewProjection", m_view_projection);
		// A transparent shader's pass runs its fragment stage, which may call LoomShadow.
		if (mesh.material->shader->settings.transparency)
			renderer.SetTexture(program, "u_shadowMap", NothingInShadow(renderer));
		renderer.SetUniform(program, "u_lightViewProjection", m_view_projection);
	};

	void Light::DrawCasters(GameObject& gameObject, MeshBatch& batch) const
	{
		if (gameObject.IsDisabled())
			return;

		// By type name, as GetComponent matches, since a script library's Mesh has its own type info.
		for (ComponentBase* component : gameObject.GetComponents())
			if (strcmp(component->GetTypeName(), typeid(Mesh).name()) == 0)
			{
				Mesh* mesh = (Mesh*)component;

				if (!mesh->IsDrawable() || !mesh->material->shadows)
					continue;

				const Shader& shader = *mesh->material->shader;
				const uint32_t program = mesh->ProgramFor(shader.shadow_id, shader.shadow_skinned_id);

				if (!program || batch.Add(*mesh, program, shader.shadow_instanced_id))
					continue;

				ReadyCaster(*mesh, program);
				mesh->Draw(program);
			}
			else if (strcmp(component->GetTypeName(), typeid(Sprite).name()) == 0)
			{
				Renderer::Get()->SetCullFace(CullFace::None);
				((Sprite*)component)->DrawShadow(m_view_projection);
			};

		for (GameObject* child : gameObject.GetChildren())
			DrawCasters(*child, batch);
	};

	#pragma endregion

	#pragma region Lighting

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
		renderer->SetUniform(program, "u_shadowTexel", m_allocated_resolution > 0 ? ACROSS_EXTENTS * shadowExtent / m_allocated_resolution : 0.0f);
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

	std::string Light::VertexLibrary(const std::string& source)
	{
		return Calls(source, { "LoomLightSpace" }) ? LIGHT_SPACE_LIBRARY : "";
	};

	std::string Light::FragmentLibrary(const std::string& source)
	{
		return VertexLibrary(source) + (Calls(source, { "LoomShadow", "LoomLight" }) ? FRAGMENT_LIBRARY : "");
	};

	#pragma endregion
};
