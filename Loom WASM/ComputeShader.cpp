#include "ComputeShader.h"

#include "Material.h"
#include "Mesh.h"
#include "Renderer.h"
#include "ShaderVariables.h"
#include "Utilities/Clock.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <iterator>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif


namespace Loom
{
	namespace
	{
		constexpr uint32_t STATIC_DRAW = 0x88E4;	// GL_STATIC_DRAW

		// Two triangles over the whole of what is drawn into.
		constexpr float QUAD[] =
		{
			-1.0f, -1.0f, 0.0f,
			1.0f, -1.0f, 0.0f,
			1.0f, 1.0f, 0.0f,
			-1.0f, -1.0f, 0.0f,
			1.0f, 1.0f, 0.0f,
			-1.0f, 1.0f, 0.0f,
		};
		constexpr size_t FLOATS_PER_VERTEX = 3;
		constexpr size_t QUAD_VERTICES = std::size(QUAD) / FLOATS_PER_VERTEX;

		constexpr const char* LAYER_UNIFORMS[ComputeShader::MAX_LAYERS] = { "u_layer0", "u_layer1", "u_layer2", "u_layer3" };
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
		m_shader_path = path;
		shader = path.empty() ? nullptr : Material::SharedShader(path);
		m_frame = 0;
	};

	void ComputeShader::OnAttach()
	{
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

	uint32_t ComputeShader::Output(const std::string& name)
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
		const int layers = std::clamp(*m_layers, 1, MAX_LAYERS);

		if ((size != m_size || layers != m_allocated) && !Allocate(size, layers))
			return;

		const uint32_t program = shader->id;
		const int steps = std::max(*m_steps, 1);
		const float delta = Time::DeltaTime() / (float)steps;
		static const UniformValues none;

		renderer->SetUniform(program, "u_time", Time::SinceStart());
		renderer->SetUniform(program, "u_deltaTime", delta);

		for (int step = 0; step < steps; step++, m_frame++)
		{
			const int writing = 1 - m_reading;

			renderer->SetUniform(program, "u_frame", UniformType::Int, &m_frame);
			ApplyShaderVariables(program, shader->variables, none, nullptr);

			// A layer the data lacks reads the first.
			for (int layer = 0; layer < MAX_LAYERS; layer++)
				renderer->SetTexture(program, LAYER_UNIFORMS[layer], m_data[m_reading][std::min(layer, layers - 1)]);

			for (int layer = 0; layer < layers; layer++)
			{
				renderer->PushTarget(m_data[writing][layer]);
				renderer->SetUniform(program, "u_writing", UniformType::Int, &layer);
				renderer->Draw(program, Mesh::Triangles, QUAD, QUAD_VERTICES, STATIC_DRAW, VertexFormat::Position);
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

	int ComputeShader::LayerNamed(const std::string& name) const
	{
		if (!m_allocated || m_shader_path->empty())
			return -1;

		const std::string stem = std::filesystem::path(GetShaderPath()).stem().string();

		if (!name.starts_with(stem))
			return -1;

		const std::string after = name.substr(stem.size());

		if (after.empty())
			return 0;

		const bool digit = after.size() == 1 && after[0] > '0' && after[0] < '0' + m_allocated;

		return digit ? after[0] - '0' : -1;
	};

	#pragma endregion
};
