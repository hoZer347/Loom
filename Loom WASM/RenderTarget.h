#pragma once

#include "Loom API.h"

#include "Component.h"
#include "Renderer.h"

#include "glm/glm.hpp"

#include <string>
#include <vector>


namespace Loom
{
	struct Material;
	struct Mesh;

	/**
	* Loom::RenderTarget
	* - A texture drawn into rather than loaded: width by height texels of
	*   eight bit RGBA, with a depth buffer behind them when depth is on.
	*   Without one nothing drawn into it is depth tested, and each draw lands
	*   on what is there by its Blend
	* - A Material reads it through a sampler2D variable whose value is the
	*   name of the target's GameObject, where an image's path would go. While
	*   something is being drawn into it, it reads as white instead, since a
	*   texture cannot be drawn into and read at once
	* - Holds what is drawn into it until Clear, which nothing calls but a
	*   script. The script draws into it when it chooses: every frame, only
	*   when something changes, or adding to what is there, which with Max
	*   keeps the brightest of everything ever drawn
	* - Made the first time it is used and cleared to clearColor, and made and
	*   cleared again once its size, depth or pixelated change
	* - Sampled smooth, or nearest when pixelated, whatever the shader reading
	*   it has pixelated set to
	*/
	struct LOOM_API RenderTarget : Component<RenderTarget, "Rendering">
	{
		// How far above and below the ground Overhead sees, the higher in front.
		static constexpr float overheadHeight = 1000.0f;

		~RenderTarget();

		// From here on, a material can name it.
		void OnAttach() override;

		// What has been drawn into it.
		void OnGui() override;

		// To clearColor, and its depth to the far plane.
		void Clear();

		// Draws mesh with its material where its GameObject puts it, seen
		// through viewProjection, which the shader is handed as
		// u_viewProjection the way a Camera hands it over in a scene. No Light
		// falls on it, so a shader with shading on shows its colour flat.
		void Draw(Mesh& mesh, const glm::mat4& viewProjection, Blend blend = Blend::Alpha);

		// The same once at each of models instead, handed over as u_model, in
		// one pass.
		void Draw(Mesh& mesh, const std::vector<glm::mat4>& models, const glm::mat4& viewProjection, Blend blend = Blend::Alpha);

		// And with material in place of the mesh's own.
		void Draw(Mesh& mesh, Material& material, const std::vector<glm::mat4>& models, const glm::mat4& viewProjection, Blend blend = Blend::Alpha);

		// The renderer's texture, made first if it has to be. 0 with no
		// renderer, or when one this size cannot be made.
		uint32_t Texture();

		bool IsDrawnInto() const { return m_drawing; };

		// The target on the GameObject called name, or null.
		static RenderTarget* Named(const std::string& name);

		// The world seen from straight above with its XZ plane laid over a
		// target, min on the first texel and max on the last, x along the rows
		// and z up them, so a shader finds a world position's texel at
		//   (world.xz - min) / (max - min)
		static glm::mat4 Overhead(const glm::vec2& min, const glm::vec2& max);

		Serial<int> width = 256;
		Serial<int> height = 256;
		Serial<bool> depth = false;
		Serial<bool> pixelated = false;
		Serial<glm::vec4> clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

	private:
		// How the texture was asked to be made.
		struct Made
		{
			int width = 0;
			int height = 0;
			bool depth = false;
			bool pixelated = false;

			bool operator==(const Made&) const = default;
		};

		// Whether there is a texture made as the fields say to draw into.
		bool Allocate();
		void Release();

		void DrawEach(Mesh& mesh, Material& material, const glm::mat4* models, size_t count, const glm::mat4& viewProjection, Blend blend);

		uint32_t m_texture = 0;

		// What was last asked for, made or not, so a size the driver refuses
		// is reported once rather than every frame.
		Made m_made;

		bool m_drawing = false;
	};
};
