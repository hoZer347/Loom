#include "Material.h"

#include "PreVertex.h"
#include "RenderMath.h"
#include "Renderer.h"
#include "Shaders.h"
#include "Utilities/Clock.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>


namespace Loom
{
	namespace
	{
		#pragma region Shader Cache

		// Shared and never freed, since ~Shader would pull the program from the other Materials.
		std::unordered_map<std::string, Shader*>& SharedShaders()
		{
			static std::unordered_map<std::string, Shader*> shaders{ };

			return shaders;
		};

		// When each failed path was written, so the editor recompiles it on change.
		std::unordered_map<std::string, std::filesystem::file_time_type>& FailedShaders()
		{
			static std::unordered_map<std::string, std::filesystem::file_time_type> failed{ };

			return failed;
		};

		Shader* Compile(const std::string& path)
		{
			try
			{
				Shader* shader = SharedShaders()[path] = new Shader(path);
				FailedShaders().erase(path);

				return shader;
			}
			catch (const std::exception& e)
			{
				SharedShaders().erase(path);

				std::error_code code;
				FailedShaders()[path] = std::filesystem::last_write_time(path, code);

				Shader::lastError = e.what();
				std::cerr << "Material: " << path << ": " << e.what() << std::endl;

				return nullptr;
			};
		};

		// A failed path recompiles here too, for the web where files cannot be watched.
		Shader* Shared(const std::string& path)
		{
			const auto found = SharedShaders().find(path);

			return found != SharedShaders().end() ? found->second : Compile(path);
		};

		#pragma endregion
	};

	#pragma region Shader

	void Material::OnAttach()
	{
		if (shader == nullptr && !m_shader_path->empty())
			shader = Shared(m_shader_path);
	};

	void Material::PickUpShader()
	{
		if (shader == nullptr && !m_shader_path->empty())
			shader = CompiledShader(m_shader_path);
	};

	void Material::ChangeShader(const std::string& path)
	{
		m_shader_path = path;
		shader = path.empty() ? nullptr : Shared(path);
	};

	void Material::ReloadChangedShaders()
	{
		for (const auto& [path, shader] : SharedShaders())
			shader->ReloadIfChanged();

		// A copy, since compiling takes the path out of the map.
		const auto failed = FailedShaders();

		for (const auto& [path, written] : failed)
		{
			std::error_code code;

			if (std::filesystem::last_write_time(path, code) != written)
				Compile(path);
		};

		PreVertex::ReloadIfChanged();
	};

	void Material::ReloadShaders(const std::function<bool(const std::string&)>& covers)
	{
		for (const auto& [path, shader] : SharedShaders())
			if (covers(path))
				shader->Reload();

		const auto failed = FailedShaders();

		for (const auto& [path, written] : failed)
			if (covers(path))
				Compile(path);
	};

	std::string Material::EditShader(const std::string& path, const std::string& source)
	{
		const std::string edited = Shader::SourceKey(path);

		Shader::SetSource(edited, source);
		Shader::lastError.clear();

		if (edited.ends_with(PreVertex::extension))
			PreVertex::ReloadIfChanged();
		else
			ReloadShaders(
				[&](const std::string& shader)
				{
					return Shader::SourceKey(shader) == edited;
				});

		return Shader::lastError;
	};

	Shader* Material::SharedShader(const std::string& path)
	{
		return Shared(path);
	};

	Shader* Material::CompiledShader(const std::string& path)
	{
		const auto found = SharedShaders().find(path);

		return found != SharedShaders().end() ? found->second : nullptr;
	};

	#pragma endregion

	#pragma region Rendering

	void Material::Apply(uint32_t program, const UniformValues* instance) const
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer)
			return;

		renderer->SetUniform(program, "u_color", *color);
		renderer->SetUniform(program, "u_shadowsOff", shadows ? 0.0f : 1.0f);
		renderer->SetUniform(program, "u_time", Time::SinceStart());

		if (!shader)
			return;

		// Set for each draw, as materials sharing a program may sample apart.
		renderer->SetPixelated(program, sampling == AsShader ? shader->settings.pixelated : sampling == Nearest);
		ApplyShaderVariables(program, shader->variables, variables, instance);
	};

	CullFace Material::Culled(bool mirrored) const
	{
		if (sides == Both)
			return CullFace::None;

		// A mirrored world matrix turns which faces are wound to the front.
		return (sides == Front) != mirrored ? CullFace::Back : CullFace::Front;
	};

	#pragma endregion

	#pragma region Editor

	void Material::OnGui()
	{
		const std::string& relative = m_shader_path;

		if (relative.empty())
			return;

		// Absolute, so the link's tooltip says which file it opens.
		std::error_code code;
		const std::string path = std::filesystem::absolute(relative, code).string();

		ImGui::TextLinkOpenURL(("Edit " + relative).c_str(), path.c_str());

		if (shader == nullptr)
			return;

		DrawSettings();
		UniformValues values = variables;

		if (DrawShaderVariables(shader->variables, UniformScope::Material, values))
			variables = std::move(values);

		if (!m_edit_error.empty())
			ImGui::TextWrapped("%s", m_edit_error.c_str());
	};

	void Material::OnFieldChanged(const SerializedField& field)
	{
		if (field.data != &*m_shader_path)
			return;

		// Until it names a shader file, the material keeps its current one.
		std::error_code code;

		if (m_shader_path->empty() ||
			(m_shader_path->ends_with(Shader::extension) && std::filesystem::is_regular_file(GetShaderPath(), code)))
			ChangeShader(m_shader_path);
	};

	void Material::DrawSettings()
	{
		ShaderSettings settings = shader->settings;

		bool changed = ImGui::Checkbox("Shading", &settings.shading);
		ImGui::SetItemTooltip("Lit by the scene's light. Written into the shader file's settings");

		changed |= ImGui::Checkbox("Shadows", &settings.shadows);
		ImGui::SetItemTooltip("Casts shadows, and is darkened by them when shading is on. Written into the shader file's settings");

		changed |= ImGui::Checkbox("Transparency", &settings.transparency);
		ImGui::SetItemTooltip("Blends by the alpha its fragment stage writes, drawn after everything opaque, and casts a shadow that dark. Written into the shader file's settings");

		changed |= ImGui::Checkbox("Pixelated", &settings.pixelated);
		ImGui::SetItemTooltip("Samples its textures nearest, so each texel stays a hard-edged square. Written into the shader file's settings");

		if (!changed)
			return;

		std::string error;
		Edited(WriteShaderSettings(shader->file_path, settings, error), error);
	};

	void Material::Edited(bool written, const std::string& error)
	{
		if (!written)
		{
			m_edit_error = error;
			std::cerr << "Material: " << error << std::endl;
			return;
		};

		m_edit_error = shader->Reload() ? "" : shader->file_path + " no longer compiles. The console says why.";
	};

	#pragma endregion
};
