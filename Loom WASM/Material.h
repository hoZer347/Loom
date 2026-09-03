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

		Shader* shader = nullptr;

	private:
		// Relative to the project folder, which is where the editor works from.
		Serial<std::string> m_shader_path;
	};
};
