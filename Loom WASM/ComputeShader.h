#pragma once

#include "Loom API.h"

#include "Asset.h"
#include "Component.h"
#include "Shaders.h"

#include "glm/glm.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>


namespace Loom
{
	/**
	* Loom::ComputeShader
	* - Runs a .loomcompute file on the GPU over a grid of cells, once a frame,
	*   or Rate times a second of game time while frames come faster, before
	*   anything is drawn, while its GameObject is enabled. Each cell holds up
	*   to four layers of a vec4, kept on the GPU in float textures
	* - The file is GLSL defining vec4 Compute(ivec2 cell, int layer), which
	*   returns one layer of one cell and reads any cell of the run before
	*   with LoomRead. The data starts as zeros, the run where u_frame is 0
	* - A shader reads the data through a sampler2D tagged "// compute" and
	*   named for the file, Swarm for Swarm.loomcompute, with a digit after
	*   the name for the second layer on, Swarm1 and the rest
	* - Where the GPU cannot draw into float textures it does not run. A web
	*   page where it cannot, or where the browser draws without the GPU,
	*   says how to turn the browser's hardware acceleration on
	*/
	struct LOOM_API ComputeShader final : Component<ComputeShader, "Rendering">
	{
		ComputeShader();
		~ComputeShader();

		#pragma region Shader

		// Names the file to run and compiles it now, for one already attached.
		void ChangeShader(const std::string& path);
		const std::string& GetShaderPath() const { return m_shader_path; };

		void OnAttach() override;
		void OnFieldChanged(const SerializedField& field) override;

		Shader* shader = nullptr;

		#pragma endregion

		#pragma region Running

		// Runs every enabled one, in the order they were made.
		static void RunAll();

		// The texture holding the layer a sampler named name reads, or 0 when
		// no enabled one is named so.
		static uint32_t Output(std::string_view name);

		// Gives back every texture, for before the renderer goes.
		static void ReleaseAll();

		static constexpr int MaxLayers = 4;

		#pragma endregion

	private:
		#pragma region Running

		void Run();
		bool Allocate(glm::ivec2 size, int layers);
		void Release();

		// Whether it and every GameObject above it are enabled.
		bool Enabled() const;

		// The layer of this one's data a sampler named name reads, or -1.
		int LayerNamed(std::string_view name) const;

		#pragma endregion

		// Relative to the project folder, which is where the editor works from.
		Serial<Asset<Shader::compute_extension>, "Shader"> m_shader_path;
		Serial<int, "Width"> m_width = 256;
		Serial<int, "Height"> m_height = 256;
		Serial<int, "Layers"> m_layers = 1;

		// Runs a frame, each reading what the one before left.
		Serial<int, "Steps"> m_steps = 1;

		// Times a second of game time it runs its Steps, at most once a frame, or every frame when 0.
		Serial<float, "Rate"> m_rate = 0.0f;

		// Each layer twice: one read while the other is written, in turn.
		static constexpr int BUFFERS = 2;
		uint32_t m_data[BUFFERS][MaxLayers]{ };
		int m_reading = 0;
		int m_allocated = 0;
		glm::ivec2 m_size{ 0 };
		int m_frame = 0;

		// Game time since it last ran, for a Rate.
		float m_waited = 0.0f;

		// The file's name without its extension, which the samplers reading it use.
		std::string m_stem;

		// What the GPU could not make, which is not tried again.
		glm::ivec2 m_failed_size{ 0 };
		int m_failed_layers = 0;

		static inline std::recursive_mutex s_mutex;
		static inline std::vector<ComputeShader*> s_all;
	};
};
