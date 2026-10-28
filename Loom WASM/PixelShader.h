#pragma once

#include "Loom API.h"

#include "Asset.h"
#include "Component.h"
#include "Shaders.h"

#include <mutex>
#include <vector>


namespace Loom
{
	/**
	* Loom::PixelShader
	* - Runs a .loompixel file over every pixel of the view each camera draws,
	*   once the view is drawn, while its GameObject is enabled
	* - Several run one after another, in the order they are drawn, each
	*   reading what the one before left
	* - The file is GLSL that writes an out vec4 colour, with the pixel pass
	*   library in front of it: LoomScenePixel, LoomSceneDepth, kernels and
	*   the rest, which the Shaders panel explains on hover
	*/
	struct LOOM_API PixelShader : Component<PixelShader, "Rendering">
	{
		PixelShader();
		~PixelShader();

		#pragma region Shader

		// Names the file to run and compiles it now, for one already attached.
		void ChangeShader(const std::string& path);
		const std::string& GetShaderPath() const { return m_shader_path; };

		void OnAttach() override;
		void OnFieldChanged(const SerializedField& field) override;

		// Whether any enabled one names a shader, so the window draws through
		// a pass only while one might run.
		static bool Any();

		Shader* shader = nullptr;

		#pragma endregion

		#pragma region Rendering

		// Notes its pass for the view being drawn.
		void OnRender() override;

		#pragma endregion

	private:
		// Relative to the project folder, which is where the editor works from.
		Serial<Asset<Shader::pixel_extension>, "Shader"> m_shader_path;

		// Whether it and every GameObject above it are enabled.
		bool Enabled() const;

		static inline std::mutex s_mutex;
		static inline std::vector<const PixelShader*> s_all;
	};
};
