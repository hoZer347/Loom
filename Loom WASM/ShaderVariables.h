#pragma once

#include "Loom API.h"

#include "UniformType.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>


namespace Loom
{
	// The lines that split a shader file into its sections. COMMON goes in
	// front of both stages. SETTINGS is not GLSL: it holds the lines
	// ShaderSettings reads.
	inline constexpr const char* COMMON_SECTION = "===COMMON===";
	inline constexpr const char* VERTEX_SECTION = "===VERTEX===";
	inline constexpr const char* FRAGMENT_SECTION = "===FRAGMENT===";
	inline constexpr const char* SETTINGS_SECTION = "===SETTINGS===";

	/**
	* Loom::ShaderSettings
	* - Which of the engine's sections a shader is compiled with, on top of the
	*   stages its file holds. Off, the stages compile as written
	* - The file's SETTINGS section says, one line each, such as
	*     shading = on
	*   and a setting the section leaves out is off
	*/
	struct ShaderSettings
	{
		// Lit by the scene's light: ambient plus diffuse, multiplied into the
		// colour the fragment stage writes.
		bool shading = false;

		// Drawn into the light's shadow map by a second program built from the
		// vertex stage, and, when shaded, darkened where the map says so.
		bool shadows = false;
	};

	// A name the engine gives a shader: a uniform Material, Mesh, Camera or
	// Light sets, or a function of the GLSL it puts in front of a stage that
	// calls one.
	struct ShaderSymbol
	{
		std::string_view name;
		std::string_view declaration;
		std::string_view description;
		bool uniform = false;
	};

	inline constexpr ShaderSymbol ENGINE_SHADER_SYMBOLS[] =
	{
		{ "u_model", "uniform mat4 u_model;", "The GameObject's world matrix, set by Mesh and Sprite.", true },
		{ "u_color", "uniform vec3 u_color;", "The Material's colour.", true },
		{ "u_shadowsOff", "uniform float u_shadowsOff;", "Not zero on a Material whose shadows are off.", true },
		{ "u_viewProjection", "uniform mat4 u_viewProjection;", "The Camera's view and projection together.", true },
		{ "u_cameraPosition", "uniform vec3 u_cameraPosition;", "The Camera's position in world space.", true },
		{ "u_lightViewProjection", "uniform mat4 u_lightViewProjection;", "The Light's view and projection, which its shadow map is drawn with.", true },
		{ "u_lightDirection", "uniform vec3 u_lightDirection;", "The way the Light shines, in world space.", true },
		{ "u_lightColor", "uniform vec3 u_lightColor;", "The Light's colour.", true },
		{ "u_ambient", "uniform float u_ambient;", "How much light reaches what the Light does not.", true },
		{ "u_shadowMap", "uniform highp sampler2DShadow u_shadowMap;", "The Light's shadow map.", true },
		{ "LoomLightSpace", "vec4 LoomLightSpace(vec4 world)", "Vertex stage: where a world position falls in the Light's shadow map. Hand it on to the fragment stage for LoomLight or LoomShadow." },
		{ "LoomShadow", "float LoomShadow(vec4 lightSpace)", "Fragment stage: 1 where the Light reaches, 0 in shadow, softened at the edges." },
		{ "LoomDiffuse", "float LoomDiffuse(vec3 normal)", "Fragment stage: how squarely the Light falls on a world space normal, from 0 to 1." },
		{ "LoomLight", "vec3 LoomLight(vec3 normal, vec4 lightSpace)\nvec3 LoomLight(vec3 normal)", "Fragment stage: the ambient light plus the Light's colour on a world space normal, shadowed when given where it falls in the shadow map." },
	};

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

	// What the source's SETTINGS section turns on. Throws std::runtime_error,
	// naming the line, at one that is not a setting.
	LOOM_API ShaderSettings ParseShaderSettings(const std::string& source);

	// Writes every setting into the shader file's SETTINGS section, over the
	// line already saying it, adding the section at the top when there is none.
	LOOM_API bool WriteShaderSettings(const std::string& path, const ShaderSettings& settings, std::string& error);

	// Hands each variable its value: a per-material one from material, a
	// per-instance one from instance when there is one, the default otherwise.
	// Every variable is set on every draw, since a program keeps the last
	// value it was given and the previous draw may have been someone else's.
	LOOM_API void ApplyShaderVariables(uint32_t program, const std::vector<ShaderVariable>& variables, const UniformValues& material, const UniformValues* instance);

	// An editor for each variable of the scope. Whether any value changed.
	LOOM_API bool DrawShaderVariables(const std::vector<ShaderVariable>& variables, UniformScope scope, UniformValues& values);
};
