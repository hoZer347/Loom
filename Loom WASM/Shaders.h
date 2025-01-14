#pragma once

#include <string>
#include <unordered_map>


namespace Loom
{
	struct Shader final
	{
		Shader();
		Shader(const std::string& file_path);

		~Shader();

		const std::string m_file_path;

		uint32_t m_id = 0; // TODO: Make this const

		////////////////////////////////////////////////////////////////////////////////////////////////////
		//
		// Temporary until generic uniform assignment is implemented
		//
		void GetShaderVec2(
			const std::string& name,
			void*& data);

		void SetShaderVec2(
			const std::string& name,
			const void* data,
			bool shouldUpdate = true);

		void SetShaderVec3(
			const std::string& name,
			const void* data,
			bool shouldUpdate = true);

		void SetShaderMat4(
			const std::string& name,
			const void* data,
			bool shouldUpdate = true);
		//
		////////////////////////////////////////////////////////////////////////////////////////////////////

		// TODO: Add generic uniform getter / setter

	protected:
		friend struct Engine;
		static void SetUniforms();

	private:
		uint32_t CompileSource(
			const std::string& vertex_shader,
			const std::string& fragment_shader);

		struct alignas(sizeof(uint64_t)) ID_Type final
		{
			uint32_t location;
			uint16_t size;
			uint16_t type;

			constexpr uint64_t as_uint64() const
			{
				return *(uint64_t*)(this);
			};
		};

		static inline std::unordered_map<
			uint32_t,				// Shader ID
			std::unordered_map<
			uint64_t,				// See "ID_Type"
			const void*				// Data

			>> m_uniforms{ }; // Used for updating uniforms

#ifdef __EMSCRIPTEN__
		void Request(
			const std::string& file_path,
			std::string& shader_source);
#endif
	};
};
