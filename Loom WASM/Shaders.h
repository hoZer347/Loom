#pragma once

#include <string>


namespace Loom
{
	struct Shader final
	{
		Shader();
		Shader(const std::string& file_path);

		~Shader();

		const std::string m_file_path;

		uint32_t m_id = 0;

		void SetShaderMat4(
			const std::string& name,
			const void* data);

		void SetShaderVec3(
			const std::string& name,
			const void* data);

		void SetShaderVec2(
			const std::string& name,
			const void* data);

		void GetShaderVec2(
			const std::string& name,
			void*& data);

		// TODO: Add generic uniform getter / setter

	private:
		void CompileSource(
			const std::string& vertex_shader,
			const std::string& fragment_shader);

#ifdef __EMSCRIPTEN__
		void Request(
			const std::string& file_path,
			std::string& shader_source);
#endif
	};
};
