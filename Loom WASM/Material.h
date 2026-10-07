#pragma once

#include "Loom API.h"

#include "Component.h"

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

		// Compiles the shader named above, unless whoever built this material has
		// already put one here.
		void OnAttach() override;

		void OnRender() override
		{ };

		// A link that opens the shader file in whatever edits that kind of file.
		void OnGui() override;

		// Hands the material's own values to its program, as u_color.
		void Apply(uint32_t program) const;

		Shader* shader = nullptr;

	private:
		// Relative to the project folder, which is where the editor works from.
		Serial<std::string> m_shader_path;

	public:
		// Declared after the path, which fixes its position in scene files.
		Serial<glm::vec3> color = glm::vec3(1.0f, 1.0f, 1.0f);
	};
};
