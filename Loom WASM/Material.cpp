#include "Material.h"

#include "EditorGui.h"
#include "GameObject.h"
#include "RenderMath.h"
#include "Renderer.h"
#include "Scene.h"
#include "Shaders.h"
#include "Utilities/Clock.h"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <utility>


namespace Loom
{
	namespace
	{
		constexpr const char* SCOPE_NAMES[] = { "Global", "Local" };

		// As wide as their choice, so the name has what is left of the row.
		constexpr ImGuiComboFlags FITTED_COMBO = ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_WidthFitPreview;

		// One Shader per path, shared by every Material that names it. Materials
		// deliberately do not own theirs: ~Shader erases the entry in Shader's own
		// program cache, so the first Material destroyed would pull the compiled
		// program out from under the rest. These live until the process ends.
		std::unordered_map<std::string, Shader*>& SharedShaders()
		{
			static std::unordered_map<std::string, Shader*> shaders{ };

			return shaders;
		};

		// When each path that failed to compile was last written, so the
		// editor's poll compiles it again once the file changes.
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

				std::cerr << "Material: " << path << ": " << e.what() << std::endl;

				return nullptr;
			};
		};

		// A path that failed is compiled again here too, so reloading the
		// scene retries it wherever the file cannot be watched (the web).
		Shader* Shared(const std::string& path)
		{
			const auto found = SharedShaders().find(path);

			return found != SharedShaders().end() ? found->second : Compile(path);
		};

		bool TypeCombo(UniformType& type)
		{
			bool changed = false;

			if (ImGui::BeginCombo("##type", InfoOf(type).glsl, FITTED_COMBO))
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

			if (ImGui::BeginCombo("##scope", SCOPE_NAMES[(int)scope], FITTED_COMBO))
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

		// A row of the variables' table: the name across what is left, then the
		// type, the scope and a button. Every row is a table of its own under
		// one ID, so a value can go between them.
		bool BeginVariableRow()
		{
			constexpr int columns = 4;

			if (!ImGui::BeginTable("##variables", columns))
				return false;

			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableNextRow();

			return true;
		};

		// Every Material on the object or under it, disabled ones included.
		template <typename Visit>
		void EachMaterial(GameObject& gameObject, const Visit& visit)
		{
			// By type name, the way GetComponent matches: a script library's
			// Material carries its own copy of the type info.
			for (ComponentBase* component : gameObject.GetComponents())
				if (strcmp(component->GetTypeName(), typeid(Material).name()) == 0)
					visit(*(Material*)component);

			for (GameObject* child : gameObject.GetChildren())
				EachMaterial(*child, visit);
		};
	};

	void Material::OnAttach()
	{
		if (shader == nullptr && !m_shader_path->empty())
			shader = Shared(m_shader_path);
	};

	void Material::PickUpShader()
	{
		if (shader != nullptr || m_shader_path->empty())
			return;

		const auto found = SharedShaders().find(m_shader_path);

		if (found != SharedShaders().end())
			shader = found->second;
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

		DrawSettings();
		DrawVariables();
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

	void Material::DrawVariables()
	{
		// Open to begin with: the values are what is edited most.
		if (!ImGui::TreeNodeEx("Shader Variables", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		// A copy: rewriting the file reloads the shader, which replaces the
		// list being drawn.
		const std::vector<ShaderVariable> declared = shader->variables;

		for (const ShaderVariable& variable : declared)
		{
			// Under the variable's row, across the inspector, so a vector's
			// components have room. Its table shares the others' ID, and with
			// it their columns' widths.
			if (!BeginVariableRow())
				continue;

			ImGui::PushID(variable.name.c_str());

			ShaderVariable changed = variable;
			std::string error;

			ImGui::TableNextColumn();
			std::string name = m_renaming == variable.name ? m_rename : variable.name;
			ImGui::SetNextItemWidth(-FLT_MIN);

			if (InputTextString("##name", name))
			{
				m_renaming = variable.name;
				m_rename = name;
			};

			if (ImGui::IsItemDeactivatedAfterEdit() && m_renaming == variable.name)
			{
				if (m_rename != variable.name)
				{
					const bool written = RenameShaderVariable(shader->file_path, variable.name, m_rename, error);

					// This one too, which may be in a scene left off the list,
					// such as the prefab viewer's.
					if (written)
					{
						RenameLocal(variable.name, m_rename);

						for (Scene* scene : Scene::GetScenes())
							EachMaterial(scene->GetRoot(),
								[&](Material& material)
								{
									if (material.shader == shader)
										material.RenameLocal(variable.name, m_rename);
								});
					};

					Edited(written, error);
				};

				m_renaming.clear();
			};

			ImGui::TableNextColumn();
			const bool retyped = TypeCombo(changed.type);

			ImGui::TableNextColumn();
			const bool rescoped = ScopeCombo(changed.scope);

			const UniformValue* found = std::as_const(variables)->Find(variable);
			const bool set = found != nullptr;
			UniformValue local = set ? *found : UniformValue::Default(variable.type);

			// A variable keeps its value across a change of scope.
			if (rescoped && changed.scope == UniformScope::Global && set)
				changed.global = local;

			if (rescoped && changed.scope == UniformScope::Local)
				SetLocal(variable.name, variable.global);

			if (retyped || rescoped)
				Edited(DeclareShaderVariable(shader->file_path, variable.name, changed, error), error);

			ImGui::TableNextColumn();

			if (ImGui::SmallButton("x"))
				Edited(RemoveShaderVariable(shader->file_path, variable.name, error), error);

			ImGui::SetItemTooltip("Take %s out of the shader file", variable.name.c_str());

			// The table pops its own ID as it ends.
			ImGui::PopID();
			ImGui::EndTable();
			ImGui::PushID(variable.name.c_str());

			if (variable.scope == UniformScope::Global)
			{
				if (DrawUniformValue(variable.type, changed.global))
					Edited(DeclareShaderVariable(shader->file_path, variable.name, changed, error), error);
			}
			else if (DrawUniformValue(variable.type, local))
				SetLocal(variable.name, local);

			ImGui::PopID();
		};

		if (BeginVariableRow())
		{
			ImGui::PushID("##new");

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			InputTextString("##name", m_new_variable.name);
			ImGui::SetItemTooltip("The new variable's name, as the shader will use it");

			ImGui::TableNextColumn();
			TypeCombo(m_new_variable.type);

			ImGui::TableNextColumn();
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

	void Material::SetLocal(const std::string& name, const UniformValue& value)
	{
		UniformValues values = variables;
		values.values[name] = value;
		variables = std::move(values);
	};

	void Material::RenameLocal(const std::string& name, const std::string& renamed)
	{
		UniformValues values = variables;
		auto value = values.values.extract(name);

		if (value.empty())
			return;

		values.values.insert_or_assign(renamed, std::move(value.mapped()));
		variables = std::move(values);
	};

	void Material::Apply(uint32_t program) const
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
		ApplyShaderVariables(program, shader->variables, variables);
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
	};
};
