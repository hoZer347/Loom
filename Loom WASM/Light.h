#pragma once

#include "Loom API.h"

#include "Component.h"

#include "glm/glm.hpp"

#include <string>


namespace Loom
{
	struct GameObject;

	/**
	* Loom::Light
	* - A directional light, and the shadow map it casts
	* - Scene::Render uses the first one in the hierarchy, renders every
	*   Mesh and shadow-casting Sprite under the scene into its shadow map,
	*   then hands it to each one's program as the main pass draws. In a scene without one, Light::ApplyNone
	*   stands in, and a lit shader shows its colour flat
	* - The shadow map covers a box of shadowExtent either side of shadowCenter,
	*   seen along direction; geometry outside it is lit
	* - A shader lights itself by calling into the GLSL the engine puts in
	*   front of a stage that uses it, when the shader is compiled:
	*     vertex:   vec4 LoomLightSpace(vec4 world)
	*                 where a world position lands in the shadow map; pass it on
	*     fragment: float LoomShadow(vec4 lightSpace)
	*                 how much light reaches the fragment, 0 to 1
	*               vec3 LoomLight(vec3 normal, vec4 lightSpace)
	*                 ambient plus the shadowed diffuse, to multiply a colour by
	* - That GLSL declares u_lightViewProjection, u_lightDirection, u_lightColor,
	*   u_ambient and u_shadowMap, so a stage that uses it must not declare them
	*/
	struct LOOM_API Light : Component<Light>
	{
		~Light();

		// The transform the shadow map is rendered with, from world space into
		// the light's clip space.
		glm::mat4 ViewProjection() const;

		// Renders every mesh under root into the shadow map. Leaves the
		// framebuffer and viewport the way it found them, so it can run inside
		// whatever target the scene is being drawn into.
		void RenderShadowMap(GameObject& root);

		// Hands the light and its shadow map to the program a mesh is about to
		// draw with.
		void Apply(uint32_t program) const;

		// For a mesh drawn with no light in its scene. A lit shader still gets a
		// depth texture behind its sampler, with nothing in shadow, and shows its
		// colour flat at full ambient.
		static void ApplyNone(uint32_t program);

		// The GLSL to put in front of a stage's source: the library above when
		// the source calls into it, otherwise nothing, so a shader that never
		// lights itself carries no shadow sampler.
		static std::string VertexLibrary(const std::string& source);
		static std::string FragmentLibrary(const std::string& source);

		// The light the scene being rendered is lit by, for as long as
		// Scene::Render is drawing it.
		static inline Light* current = nullptr;

		LOOM_SERIAL(Math::vec3<float>, direction, Math::vec3<float>(-0.4f, -1.0f, -0.3f));
		LOOM_SERIAL(Math::vec3<float>, color, Math::vec3<float>(1.0f, 1.0f, 1.0f));
		LOOM_SERIAL(float, intensity, 1.0f);
		LOOM_SERIAL(float, ambient, 0.25f);

		// Off still allocates the map, cleared to nothing in shadow: a shader
		// that declares the sampler has to have a depth texture behind it.
		LOOM_SERIAL(bool, castShadows, true);
		// Clamped to what the driver can allocate.
		LOOM_SERIAL(int, shadowResolution, 2048);
		LOOM_SERIAL(Math::vec3<float>, shadowCenter);
		LOOM_SERIAL(float, shadowExtent, 8.0f);

	private:
		// Whether there is a shadow map of the current resolution to draw into.
		bool Allocate();
		void Release();

		void DrawCasters(GameObject& gameObject, uint32_t program) const;

		uint32_t m_depth_texture = 0;

		// What was last asked for, failed or not, so a resolution the driver
		// refuses is reported once rather than every frame.
		int m_allocated_resolution = 0;

		glm::mat4 m_view_projection = glm::mat4(1.0f);
	};
};
