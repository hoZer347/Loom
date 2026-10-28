#include "PixelShader.h"

#include "Material.h"
#include "PixelPass.h"
#include "Renderer.h"
#include "Utilities/Clock.h"

#include <algorithm>
#include <filesystem>


namespace Loom
{
	PixelShader::PixelShader()
	{
		std::scoped_lock lock{ s_mutex };
		s_all.push_back(this);
	};

	PixelShader::~PixelShader()
	{
		std::scoped_lock lock{ s_mutex };
		std::erase(s_all, this);
	};

	#pragma region Shader

	void PixelShader::ChangeShader(const std::string& path)
	{
		m_shader_path = path;
		shader = path.empty() ? nullptr : Material::SharedShader(path);
	};

	void PixelShader::OnAttach()
	{
		if (shader == nullptr && !m_shader_path->empty())
			shader = Material::SharedShader(m_shader_path);
	};

	void PixelShader::OnFieldChanged(const SerializedField& field)
	{
		if (field.data != &*m_shader_path)
			return;

		std::error_code code;

		if (m_shader_path->empty() || (Shader::IsPixel(m_shader_path) && std::filesystem::is_regular_file(GetShaderPath(), code)))
			ChangeShader(m_shader_path);
	};

	bool PixelShader::Any()
	{
		std::scoped_lock lock{ s_mutex };

		return std::any_of(
			s_all.begin(),
			s_all.end(),
			[](const PixelShader* pixel)
			{
				return (pixel->shader != nullptr || !pixel->m_shader_path->empty()) && pixel->Enabled();
			});
	};

	bool PixelShader::Enabled() const
	{
		if (!GetGameObject())
			return false;

		for (const GameObject* above = GetGameObject(); above != nullptr; above = above->GetParent())
			if (above->IsDisabled())
				return false;

		return true;
	};

	#pragma endregion

	#pragma region Rendering

	void PixelShader::OnRender()
	{
		if (shader == nullptr && !m_shader_path->empty())
			shader = Material::CompiledShader(m_shader_path);

		if (shader == nullptr)
			return;

		if (Renderer* renderer = Renderer::Get())
			renderer->SetUniform(shader->id, "u_time", Time::SinceStart());

		PixelPass::Note(shader->id);
	};

	#pragma endregion
};
