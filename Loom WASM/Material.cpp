#include "Material.h"

#include "EditorGui.h"
#include "RenderMath.h"
#include "Renderer.h"
#include "Shaders.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>


namespace Loom
{
	namespace
	{
		constexpr const char* SCOPE_NAMES[] = { "Material", "Instance" };

		// One Shader per path, shared by every Material that names it. Materials
		// deliberately do not own theirs: ~Shader erases the entry in Shader's own
		// program cache, so the first Material destroyed would pull the compiled
		// program out from under the rest. These live until the process ends.
		std::unordered_map<std::string, Shader*>& SharedShaders()
		{
			static std::unordered_map<std::string, Shader*> shaders{ };

			return shaders;
		};

		Shader* Shared(const std::string& path)
		{
			std::unordered_map<std::string, Shader*>& shaders = SharedShaders();

			const auto found = shaders.find(path);

			if (found != shaders.end())
				return found->second;

			try
			{
				return shaders[path] = new Shader(path);
			}
			catch (const std::exception& e)
			{
				std::cerr << "Material: " << e.what() << std::endl;

				// Deliberately not remembered: fixing the shader file and
				// reloading the scene should be enough, without restarting the
				// editor. Shader's own registry does the same.
				return nullptr;
			};
		};

		bool TypeCombo(UniformType& type)
		{
			bool changed = false;

			if (ImGui::BeginCombo("##type", InfoOf(type).glsl))
			{
				for (const UniformTypeInfo& info : UNIFORM_TYPES)
					if (ImGui::Selectable(info.glsl, info.type == type))
					{
						changed = info.type != type;
						type = info.type;
					};

				ImGui::EndCombo();
			};

			return changed;
		};

		bool ScopeCombo(UniformScope& scope)
		{
			bool changed = false;

			if (ImGui::BeginCombo("##scope", SCOPE_NAMES[(int)scope]))
			{
				for (int i = 0; i < (int)std::size(SCOPE_NAMES); i++)
					if (ImGui::Selectable(SCOPE_NAMES[i], i == (int)scope))
					{
						changed = i != (int)scope;
						scope = (UniformScope)i;
					};

				ImGui::EndCombo();
			};

			return changed;
		};
	};

	void Material::OnAttach()
	{
		if (shader == nullptr && !m_shader_path->empty())
			shader = Shared(m_shader_path);
	};

	void Material::ChangeShader(const std::string& path)
	{
		m_shader_path = path;
		shader = path.empty() ? nullptr : Shared(path);
	};

	void Material::OnFieldChanged(const SerializedField& field)
	{
		if (field.data != &*m_shader_path)
			return;

		// The field changes a letter at a time. Until it names a shader file,
		// the material keeps drawing with the one it has.
		std::error_code code;

		if (m_shader_path->empty() ||
			(m_shader_path->ends_with(Shader::extension) && std::filesystem::is_regular_file(*m_shader_path, code)))
			ChangeShader(m_shader_path);
	};

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

		DrawShaderVariables(shader->variables, UniformScope::Material, variables);
		DrawDeclarations();
	};

	void Material::DrawDeclarations()
	{
		// Open to begin with: adding a variable is the reason to look here.
		if (!ImGui::TreeNodeEx("Shader Variables", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		constexpr int COLUMNS = 4;

		// Wide enough for the longest choice and the combo's arrow.
		const auto combo_width = [](const char* longest)
			{
				return ImGui::CalcTextSize(longest).x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2;
			};

		if (ImGui::BeginTable("##declarations", COLUMNS))
		{
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, combo_width(InfoOf(UniformType::Sampler2D).glsl));
			ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthFixed, combo_width(SCOPE_NAMES[(int)UniformScope::Material]));
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);

			// A copy: rewriting the file recompiles the shader, which replaces
			// the list being drawn.
			const std::vector<ShaderVariable> declared = shader->variables;

			for (const ShaderVariable& variable : declared)
			{
				ImGui::PushID(variable.name.c_str());
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(variable.name.c_str());

				ShaderVariable changed = variable;
				std::string error;

				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-FLT_MIN);
				const bool retyped = TypeCombo(changed.type);

				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-FLT_MIN);
				const bool rescoped = ScopeCombo(changed.scope);

				if (retyped || rescoped)
					Edited(DeclareShaderVariable(shader->file_path, variable.name, changed, error), error);

				ImGui::TableNextColumn();

				if (ImGui::SmallButton("x"))
					Edited(RemoveShaderVariable(shader->file_path, variable.name, error), error);

				ImGui::SetItemTooltip("Take %s out of the shader file", variable.name.c_str());

				ImGui::PopID();
			};

			ImGui::PushID("##new");
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			InputTextString("##name", m_new_variable.name);
			ImGui::SetItemTooltip("The new variable's name, as the shader will use it");

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			TypeCombo(m_new_variable.type);

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			ScopeCombo(m_new_variable.scope);

			ImGui::TableNextColumn();
			ImGui::BeginDisabled(m_new_variable.name.empty());

			if (ImGui::SmallButton("+"))
			{
				std::string error;
				const bool written = DeclareShaderVariable(shader->file_path, "", m_new_variable, error);

				Edited(written, error);

				if (written)
					m_new_variable.name.clear();
			};

			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Declare it in the shader file");

			ImGui::PopID();
			ImGui::EndTable();
		};

		if (!m_edit_error.empty())
			ImGui::TextWrapped("%s", m_edit_error.c_str());

		ImGui::TreePop();
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

	void Material::Apply(uint32_t program, const UniformValues* instance) const
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer)
			return;

		renderer->SetUniform(program, "u_color", *color);
		renderer->SetUniform(program, "u_shadowsOff", shadows ? 0.0f : 1.0f);

		if (shader)
			ApplyShaderVariables(program, shader->variables, variables, instance);
	};

	void Material::ReloadChangedShaders()
	{
		for (const auto& [path, shader] : SharedShaders())
			shader->ReloadIfChanged();
	};
};
