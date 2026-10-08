#pragma once

#include "Loom API.h"

#include "Component.h"
#include "ShaderVariables.h"

#include <string>


namespace Loom
{
	struct Shader;

	struct LOOM_API Material : Component<Material>
	{
		// Names the shader to compile. Read by OnAttach, so a script sets it on
		// the material it just attached; changing it after that takes effect the
		// next time the material is attached, which for a scene means reloading.
		void SetShaderPath(const std::string& path) { m_shader_path = path; };
		const std::string& GetShaderPath() const { return m_shader_path; };

		// Names the shader and compiles it now, for a material already attached.
		// The editor's way, on the thread that owns the renderer.
		void ChangeShader(const std::string& path);

		// Compiles the shader named above, unless whoever built this material has
		// already put one here.
		void OnAttach() override;

		void OnRender() override
		{ };

		// A link that opens the shader file, the values of its per-material
		// variables, and the declarations themselves, which are edited in the
		// file.
		void OnGui() override;

		void OnFieldChanged(const SerializedField& field) override;

		// Hands the material's values to its program: u_color, whether it is
		// shadowed, and the shader's variables, the per-instance ones from
		// instance.
		void Apply(uint32_t program, const UniformValues* instance = nullptr) const;

		// Recompiles every material's shader whose file has been written since,
		// for after something outside the editor edits one.
		static void ReloadChangedShaders();

		Shader* shader = nullptr;

	private:
		// Relative to the project folder, which is where the editor works from.
		Serial<std::string> m_shader_path;

	public:
		// Declared after the path, which fixes its position in scene files.
		Serial<glm::vec3> color = glm::vec3(1.0f, 1.0f, 1.0f);

		// Off, what is drawn with it neither casts shadows nor has them cast on it.
		Serial<bool> shadows = true;

		// The per-material variables' values.
		Serial<UniformValues> variables;

	private:
		void DrawDeclarations();

		// After the shader file has been rewritten, or failed to be, with why
		// in error.
		void Edited(bool written, const std::string& error);

		// The declaration the "Add" row is building.
		ShaderVariable m_new_variable;

		// Why the last edit to the shader file failed, shown until the next one.
		std::string m_edit_error;
	};
};
