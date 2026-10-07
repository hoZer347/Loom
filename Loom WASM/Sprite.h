#pragma once

#include "Loom API.h"

#include "Component.h"

#include "glm/glm.hpp"

#include <string>


namespace Loom
{
	struct Texture;

	/**
	* Loom::Sprite
	* - An image on a quad in the GameObject's XY plane, facing +Z, sized by
	*   its pixels and pixelsPerUnit, and placed so pivot (0 to 1 across the
	*   image) sits on the GameObject's origin
	* - Pixels whose alpha, times color's, falls under alphaCutoff are cut out:
	*   nothing is drawn there and nothing behind is hidden
	* - Lit by the scene's Light and shadowed by everything else, from either
	*   side, the way a thin card would be
	* - With castShadows, the Light's shadow pass draws it with the same
	*   cutout, so the shadow has the image's shape, holes and all, and follows
	*   it as the image, the tint or the cutoff change
	*/
	struct LOOM_API Sprite : Component<Sprite>
	{
		void OnRender() override;

		// The texture named by texturePath, loading it the first time it is asked
		// for and again whenever the path changes. Null while it will not load.
		Texture* GetTexture();

		// The quad's own space, a unit square from 0 to 1, to world space.
		glm::mat4 QuadMatrix();

		// Draws the cutout into a shadow map the light has bound. Does nothing
		// when castShadows is off or there is no texture.
		void DrawShadow(const glm::mat4& lightViewProjection);

		// Relative to the project folder, which is where the editor works from.
		LOOM_SERIAL(std::string, texturePath);
		LOOM_SERIAL(Math::vec4<float>, color, Math::vec4<float>{ { 1.0f, 1.0f, 1.0f, 1.0f } });
		LOOM_SERIAL(float, pixelsPerUnit, 100.0f);
		LOOM_SERIAL(Math::vec2<float>, pivot, Math::vec2<float>{ { 0.5f, 0.0f } });
		LOOM_SERIAL(float, alphaCutoff, 0.5f);
		LOOM_SERIAL(bool, castShadows, true);

	private:
		// Draws the quad with program, the texture and cutoff already handed to it.
		void DrawQuad(uint32_t program);

		Texture* m_texture = nullptr;

		// What was last asked for, loaded or not, so a file that will not load
		// is reported once rather than every frame.
		std::string m_loaded_path;
	};
};
