#pragma once

#include "Loom API.h"

#include "UniformType.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>


namespace Loom
{
	// The lines that split a shader file into its sections. COMMON goes in
	// front of both stages.
	inline constexpr const char* COMMON_SECTION = "===COMMON===";
	inline constexpr const char* VERTEX_SECTION = "===VERTEX===";
	inline constexpr const char* FRAGMENT_SECTION = "===FRAGMENT===";

	// Whether a variable is filled in once on the Material, or on each Mesh
	// drawn with it.
	enum class UniformScope { Material, Instance };

	/**
	* Loom::ShaderVariable
	* - A uniform a shader declares for whoever uses it to fill in, as opposed
	*   to one the engine sets (u_model, u_color, the camera's and the light's)
	* - The shader file is where they live: each is a line such as
	*     uniform vec3 u_tint; // instance
	*   which the editor writes into the file's COMMON section. Without the
	*   comment, or with "// material", it is per material
	*/
	struct ShaderVariable
	{
		std::string name;
		UniformType type = UniformType::Float;
		UniformScope scope = UniformScope::Material;
	};

	struct LOOM_API UniformValue
	{
		UniformType type = UniformType::Float;

		// The type's columns one after another. A double holds every float,
		// int and unsigned int exactly.
		std::array<double, MAX_UNIFORM_COMPONENTS> components{ };

		// A sampler's image, relative to the project folder.
		std::string texture;

		// Zero, or the identity for a matrix.
		static UniformValue Default(UniformType type);
	};

	/**
	* Loom::UniformValues
	* - What a Material or a Mesh fills its shader's variables in with, by name
	* - Keeps a value its shader no longer declares, so taking a variable out
	*   and putting it back loses nothing
	*/
	struct LOOM_API UniformValues
	{
		// What was set for the variable, or null when nothing of its type was.
		const UniformValue* Find(const ShaderVariable& variable) const;

		void Set(const std::string& name, UniformType type, std::initializer_list<double> components);
		void SetTexture(const std::string& name, const std::string& path);

		// One line a value: its name, its type, then its components or, for a
		// sampler, the image's path.
		std::string Write() const;
		void Read(const std::string& text);

		std::map<std::string, UniformValue> values;
	};

	// Every uniform the source declares, bar the ones the engine sets.
	LOOM_API std::vector<ShaderVariable> ParseShaderVariables(const std::string& source);

	// Writes the variable's declaration into the shader file: over the line
	// declaring replacing when it names one, otherwise into the COMMON
	// section, which it adds when the file has none. False, with why in error,
	// when the name is not one a variable can have or the file cannot be
	// written.
	LOOM_API bool DeclareShaderVariable(const std::string& path, const std::string& replacing, const ShaderVariable& variable, std::string& error);

	// Takes the line declaring the variable out of the shader file.
	LOOM_API bool RemoveShaderVariable(const std::string& path, const std::string& name, std::string& error);

	// Hands each variable its value: a per-material one from material, a
	// per-instance one from instance when there is one, the default otherwise.
	// Every variable is set on every draw, since a program keeps the last
	// value it was given and the previous draw may have been someone else's.
	LOOM_API void ApplyShaderVariables(uint32_t program, const std::vector<ShaderVariable>& variables, const UniformValues& material, const UniformValues* instance);

	// An editor for each variable of the scope. Whether any value changed.
	LOOM_API bool DrawShaderVariables(const std::vector<ShaderVariable>& variables, UniformScope scope, UniformValues& values);
};
