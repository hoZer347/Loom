#include "ComputeShader.h"

#include "Material.h"
#include "Mesh.h"
#include "PixelPass.h"
#include "Renderer.h"
#include "ShaderVariables.h"
#include "Utilities/Clock.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif


namespace Loom
{
	namespace
	{
		constexpr const char* LAYER_UNIFORMS[ComputeShader::MaxLayers] = { "u_layer0", "u_layer1", "u_layer2", "u_layer3" };
		constexpr float ZEROS[] = { 0.0f, 0.0f, 0.0f, 0.0f };

#ifdef __EMSCRIPTEN__
		// The page's notice, which Player.html defines, shown when the GPU
		// cannot run compute shaders or the browser draws without it.
		EM_JS(void, CheckPageGpu, (bool drawable), {
			if (Module.loomCheckGpu)
				Module.loomCheckGpu(drawable);
		});
#endif

		// Once a run, as the first ComputeShader takes its textures.
		void CheckGpu(bool drawable)
		{
			static bool checked = false;

			if (checked)
				return;

			checked = true;

			if (!drawable)
				std::cerr << "ComputeShader: this GPU cannot draw into float textures, so compute shaders do not run" << std::endl;

#ifdef __EMSCRIPTEN__
			CheckPageGpu(drawable);
#endif
		};
	};

	ComputeShader::ComputeShader()
	{
		std::scoped_lock lock{ s_mutex };
		s_all.push_back(this);
	};

	ComputeShader::~ComputeShader()
	{
		Release();

		std::scoped_lock lock{ s_mutex };
		std::erase(s_all, this);
	};

	#pragma region Shader

	void ComputeShader::ChangeShader(const std::string& path)
	{
		Release();
		m_shader_path = path;
		m_stem = std::filesystem::path(path).stem().string();
		shader = path.empty() ? nullptr : Material::SharedShader(path);
		m_frame = 0;
	};

	void ComputeShader::OnAttach()
	{
		m_stem = std::filesystem::path(GetShaderPath()).stem().string();

		if (shader == nullptr && !m_shader_path->empty())
			shader = Material::SharedShader(m_shader_path);
	};

	void ComputeShader::OnFieldChanged(const SerializedField& field)
	{
		if (field.data != &*m_shader_path)
			return;

		std::error_code code;

		if (m_shader_path->empty() || (Shader::IsCompute(m_shader_path) && std::filesystem::is_regular_file(GetShaderPath(), code)))
			ChangeShader(m_shader_path);
	};

	#pragma endregion

	#pragma region Running

	void ComputeShader::RunAll()
	{
		std::scoped_lock lock{ s_mutex };

		for (ComputeShader* compute : s_all)
			compute->Run();
	};

	uint32_t ComputeShader::Output(std::string_view name)
	{
		std::scoped_lock lock{ s_mutex };

		// Swarm2's first layer before Swarm's third.
		uint32_t later = 0;

		for (const ComputeShader* compute : s_all)
		{
			const int layer = compute->LayerNamed(name);

			if (layer < 0 || !compute->Enabled())
				continue;

			if (layer == 0)
				return compute->m_data[compute->m_reading][layer];

			if (!later)
				later = compute->m_data[compute->m_reading][layer];
		};

		return later;
	};

	void ComputeShader::ReleaseAll()
	{
		std::scoped_lock lock{ s_mutex };

		for (ComputeShader* compute : s_all)
			compute->Release();
	};

	void ComputeShader::Run()
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer || !Enabled())
			return;

		if (shader == nullptr && !m_shader_path->empty())
			shader = Material::CompiledShader(m_shader_path);

		if (shader == nullptr || !shader->id)
			return;

		const int most = renderer->MaxTextureSize();
		const glm::ivec2 size(std::clamp(*m_width, 1, most), std::clamp(*m_height, 1, most));
		const int layers = std::clamp(*m_layers, 1, MaxLayers);

		if (size == m_failed_size && layers == m_failed_layers)
			return;

		if ((size != m_size || layers != m_allocated) && !Allocate(size, layers))
			return;

		float covered = Time::DeltaTime();

		if (*m_rate > 0.0f)
		{
			const float interval = 1.0f / *m_rate;

			m_waited += covered;

			if (m_waited < interval)
				return;

			m_waited = std::min(m_waited - interval, interval);
			covered = interval;
		};

		const uint32_t program = shader->id;
		const int steps = std::max(*m_steps, 1);
		const float delta = covered / (float)steps;
		static const UniformValues none;

		renderer->SetUniform(program, "u_time", Time::SinceStart());
		renderer->SetUniform(program, "u_deltaTime", delta);

		for (int step = 0; step < steps; step++, m_frame++)
		{
			const int writing = 1 - m_reading;

			renderer->SetUniform(program, "u_frame", UniformType::Int, &m_frame);
			ApplyShaderVariables(program, shader->variables, none, nullptr);

			// A layer the data lacks reads the first.
			for (int layer = 0; layer < MaxLayers; layer++)
				renderer->SetTexture(program, LAYER_UNIFORMS[layer], m_data[m_reading][std::min(layer, layers - 1)]);

			for (int layer = 0; layer < layers; layer++)
			{
				renderer->PushTarget(m_data[writing][layer]);
				renderer->SetUniform(program, "u_writing", UniformType::Int, &layer);
				renderer->Draw(program, Mesh::Triangles, PixelPass::Quad, PixelPass::QuadVertices, Mesh::Static, VertexFormat::Position);
				renderer->PopTarget();
			};

			m_reading = writing;
		};
	};

	bool ComputeShader::Allocate(glm::ivec2 size, int layers)
	{
		Release();

		Renderer* renderer = Renderer::Get();

		for (auto& buffer : m_data)
			for (int layer = 0; layer < layers; layer++)
			{
				buffer[layer] = renderer->CreateDataTarget(size.x, size.y);

				if (!buffer[layer])
				{
					Release();
					m_failed_size = size;
					m_failed_layers = layers;
					CheckGpu(false);
					return false;
				};

				renderer->PushTarget(buffer[layer]);
				renderer->Clear(ZEROS, false);
				renderer->PopTarget();
			};

		m_size = size;
		m_allocated = layers;
		m_reading = 0;
		m_frame = 0;
		CheckGpu(true);

		return true;
	};

	void ComputeShader::Release()
	{
		Renderer* renderer = Renderer::Get();

		for (auto& buffer : m_data)
			for (uint32_t& texture : buffer)
			{
				if (renderer && texture)
					renderer->DestroyTexture(texture);

				texture = 0;
			};

		m_size = glm::ivec2(0);
		m_allocated = 0;
	};

	bool ComputeShader::Enabled() const
	{
		if (!GetGameObject())
			return false;

		for (const GameObject* above = GetGameObject(); above != nullptr; above = above->GetParent())
			if (above->IsDisabled())
				return false;

		return true;
	};

	int ComputeShader::LayerNamed(std::string_view name) const
	{
		if (!m_allocated || m_stem.empty() || !name.starts_with(m_stem))
			return -1;

		const std::string_view after = name.substr(m_stem.size());

		if (after.empty())
			return 0;

		const bool digit = after.size() == 1 && after[0] > '0' && after[0] < '0' + m_allocated;

		return digit ? after[0] - '0' : -1;
	};

	#pragma endregion
};
