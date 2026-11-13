#pragma once

#include "Loom API.h"

#include "UniformType.h"

#include "glm/glm.hpp"

#include <array>
#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>


namespace Loom
{
	#pragma region Sections

	// The lines that split a shader file into its sections. COMMON goes in
	// front of both stages. SETTINGS is not GLSL: it holds the lines
	// ShaderSettings reads.
	inline constexpr const char* COMMON_SECTION = "===COMMON===";
	inline constexpr const char* VERTEX_SECTION = "===VERTEX===";
	inline constexpr const char* FRAGMENT_SECTION = "===FRAGMENT===";
	inline constexpr const char* SETTINGS_SECTION = "===SETTINGS===";
	inline constexpr const char* PIXEL_PASS_SECTION = "===PIXEL PASS===";

	#pragma endregion

	#pragma region Settings

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
		// colour the fragment stage writes. A fragment stage that calls
		// LoomLight lights itself, so it is left as written, as with shading off.
		bool shading = false;

		// Drawn into the light's shadow map by a second program built from the
		// vertex stage, and, when shaded, darkened where the map says so. With
		// transparency on, the fragment stage joins it, and the shadow is as
		// dark as its alpha.
		bool shadows = false;

		// Drawn after everything opaque, farthest first, each depth before
		// colour, so what is behind it shows through wherever its alpha is under 1.
		bool transparency = false;

		// Its colour textures are sampled nearest, so each texel draws as a
		// hard-edged square however close it is, the way pixel art and
		// PS1-era models want.
		bool pixelated = false;
	};

	// What the source's SETTINGS section turns on. Throws std::runtime_error,
	// naming the line, at one that is not a setting.
	LOOM_API ShaderSettings ParseShaderSettings(const std::string& source);

	// Writes every setting into the shader file's SETTINGS section, over the
	// line already saying it, adding the section at the top when there is none.
	LOOM_API bool WriteShaderSettings(const std::string& path, const ShaderSettings& settings, std::string& error);

	#pragma endregion

	#pragma region Variables

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
		{ "u_time", "uniform float u_time;", "Seconds of game time since the start, slowed and stopped with Time's scale.", true },
		{ "u_seed", "uniform float u_seed;", "A number from 0 up to 1 of the GameObject's own, the same every frame, and every run for one saved in the scene, so instances can differ.", true },
		{ "u_viewProjection", "uniform mat4 u_viewProjection;", "The Camera's view and projection together.", true },
		{ "u_cameraPosition", "uniform vec3 u_cameraPosition;", "The Camera's position in world space.", true },
		{ "u_lightViewProjection", "uniform mat4 u_lightViewProjection;", "The Light's view and projection, which its shadow map is drawn with.", true },
		{ "u_shadowTexel", "uniform float u_shadowTexel;", "World units across one texel of the Light's shadow map.", true },
		{ "u_lightDirection", "uniform vec3 u_lightDirection;", "The way the Light shines, in world space.", true },
		{ "u_lightColor", "uniform vec3 u_lightColor;", "The Light's colour.", true },
		{ "u_ambient", "uniform float u_ambient;", "How much light reaches what the Light does not.", true },
		{ "u_shadowMap", "uniform highp sampler2DShadow u_shadowMap;", "The Light's shadow map.", true },
		{ "LoomLightSpace", "vec4 LoomLightSpace(vec4 world)\nvec4 LoomLightSpace(vec4 world, vec3 normal)", "Vertex stage: where a world position falls in the Light's shadow map, or a little out from it along a world space normal, so a smooth surface does not shadow itself. Hand it on to the fragment stage for LoomLight or LoomShadow." },
		{ "LoomShadow", "float LoomShadow(vec4 lightSpace)", "Fragment stage: 1 where the Light reaches, 0 in shadow, softened at the edges." },
		{ "LoomDiffuse", "float LoomDiffuse(vec3 normal)", "Fragment stage: how squarely the Light falls on a world space normal, from 0 to 1." },
		{ "LoomLight", "vec3 LoomLight(vec3 normal, vec4 lightSpace)\nvec3 LoomLight(vec3 normal)", "Fragment stage: the ambient light plus the Light's colour on a world space normal, shadowed when given where it falls in the shadow map." },
		{ "u_scene", "uniform highp sampler2D u_scene;", "Pixel pass: the view as drawn, or as the pass before this one left it.", true },
		{ "u_sceneDepth", "uniform highp sampler2D u_sceneDepth;", "Pixel pass: the view's depth, 0 at the near plane and 1 at the far.", true },
		{ "u_frame", "uniform int u_frame;", "Compute: how many times the ComputeShader has run, 0 the first time, when its data is all zeros.", true },
		{ "u_deltaTime", "uniform float u_deltaTime;", "Compute: seconds of game time one run covers, the frame's or a Rate's interval divided by Steps.", true },
		{ "u_writing", "uniform int u_writing;", "Compute: the layer being written, which Compute is handed as layer.", true },
		{ "u_layer0", "uniform highp sampler2D u_layer0;", "Compute: the first layer as the run before left it. LoomRead reads any layer.", true },
		{ "u_layer1", "uniform highp sampler2D u_layer1;", "Compute: the second layer as the run before left it.", true },
		{ "u_layer2", "uniform highp sampler2D u_layer2;", "Compute: the third layer as the run before left it.", true },
		{ "u_layer3", "uniform highp sampler2D u_layer3;", "Compute: the fourth layer as the run before left it.", true },
		{ "Compute", "vec4 Compute(ivec2 cell, int layer)", "Compute: defined by the file, the new value of one layer of one cell, run once for each." },
		{ "LoomRead", "vec4 LoomRead(ivec2 cell, int layer)\nvec4 LoomRead(int index, int layer)", "Compute: a cell's layer as the run before left it, wrapping around the edges." },
		{ "LoomSize", "ivec2 LoomSize()", "Compute: how many cells across and up the data is." },
		{ "LoomCount", "int LoomCount()", "Compute: how many cells the data holds." },
		{ "LoomIndex", "int LoomIndex(ivec2 cell)", "Compute: a cell's place counting along each row from the first." },
		{ "LoomCell", "ivec2 LoomCell(int index)", "Compute: the cell at a place counting along each row from the first." },
		{ "LoomScreenPixel", "ivec2 LoomScreenPixel()", "Pixel pass: the pixel being drawn." },
		{ "LoomScenePixel", "vec4 LoomScenePixel(ivec2 pixel)", "Pixel pass: the view's colour at a pixel." },
		{ "LoomSceneDepth", "float LoomSceneDepth(ivec2 pixel)", "Pixel pass: the view's depth at a pixel, 0 at the near plane and 1 at the far or where nothing was drawn." },
		{ "LoomScenePosition", "vec3 LoomScenePosition(ivec2 pixel)", "Pixel pass: where in the world a pixel of the view is." },
		{ "LoomSceneNormal", "vec3 LoomSceneNormal(ivec2 pixel)", "Pixel pass: the world space normal of the surface at a pixel, facing the camera, or zero where nothing was drawn." },
		{ "PixelKernel", "PixelKernel_3x3 name;", "Pixel pass: declared outside any function, the colours of the 3x3 pixels (any odd size up to 15) around the one being drawn, as name.pixels, with name.offsets, name.weights (a Gaussian), name.centre and name.count." },
		{ "PositionKernel", "PositionKernel_3x3 name;", "Pixel pass: as PixelKernel, with the world positions of the pixels as name.positions." },
		{ "NormalKernel", "NormalKernel_3x3 name;", "Pixel pass: as PixelKernel, with the world space normals of the pixels as name.normals." },
	};

	// Whether a variable is filled in once on the Material, on each Mesh
	// drawn with it, or, for a sampler2D, by the ComputeShader whose file it
	// is named for, a digit after the name for a layer past the first.
	enum class UniformScope { Material, Instance, Compute };

	/**
	* Loom::ShaderVariable
	* - A uniform a shader declares for whoever uses it to fill in, as opposed
	*   to one the engine sets (u_model, u_color, the camera's and the light's)
	* - The shader file is where they live: each is a line such as
	*     uniform vec3 u_tint; // instance
	*   Without the comment, or with "// material", it is per material, and
	*   with "// compute" it is a compute shader's data
	*/
	struct ShaderVariable
	{
		std::string name;
		UniformType type = UniformType::Float;
		UniformScope scope = UniformScope::Material;
	};

	// Every uniform the source declares, bar the ones the engine sets.
	LOOM_API std::vector<ShaderVariable> ParseShaderVariables(const std::string& source);

	#pragma endregion

	#pragma region Values

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

		bool operator==(const UniformValue&) const = default;
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

		bool operator==(const UniformValues&) const = default;
	};

	// Hands each variable its value: a per-material one from material, a
	// per-instance one from instance when there is one, the default otherwise.
	// Every variable is set on every draw, since a program keeps the last
	// value it was given and the previous draw may have been someone else's.
	LOOM_API void ApplyShaderVariables(uint32_t program, const std::vector<ShaderVariable>& variables, const UniformValues& material, const UniformValues* instance);

	// An editor for each variable of the scope. Whether any value changed.
	LOOM_API bool DrawShaderVariables(const std::vector<ShaderVariable>& variables, UniformScope scope, UniformValues& values);

	#pragma endregion

	#pragma region Typed Values

	// The columns and rows of a value a script hands a variable: a number, a
	// glm vector or a glm matrix.
	template <typename T>
	struct UniformShape
	{
		using Scalar = T;
		static constexpr int Columns = 1;
		static constexpr int Rows = 1;
	};

	template <glm::length_t L, typename T, glm::qualifier Q>
	struct UniformShape<glm::vec<L, T, Q>>
	{
		using Scalar = T;
		static constexpr int Columns = 1;
		static constexpr int Rows = L;
	};

	template <glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
	struct UniformShape<glm::mat<C, R, T, Q>>
	{
		using Scalar = T;
		static constexpr int Columns = C;
		static constexpr int Rows = R;
	};

	// The type a variable holding a T is declared as, or nothing when no
	// variable can hold one.
	template <typename T>
	constexpr std::optional<UniformType> UniformTypeOf()
	{
		using Scalar = typename UniformShape<T>::Scalar;
		UniformKind kind;

		if constexpr (std::same_as<Scalar, bool>)
			kind = UniformKind::Bool;
		else if constexpr (std::floating_point<Scalar>)
			kind = UniformKind::Float;
		else if constexpr (std::signed_integral<Scalar>)
			kind = UniformKind::Int;
		else if constexpr (std::unsigned_integral<Scalar>)
			kind = UniformKind::UInt;
		else
			return std::nullopt;

		for (const UniformTypeInfo& info : UNIFORM_TYPES)
			if (info.kind == kind && info.columns == UniformShape<T>::Columns && info.rows == UniformShape<T>::Rows)
				return info.type;

		return std::nullopt;
	};

	// A number, vector or matrix a variable can hold, or a sampler's image path.
	template <typename T>
	concept AUniform = UniformTypeOf<T>().has_value() || std::convertible_to<const T&, std::string>;

	template <typename T>
	auto& UniformComponent(T& value, int column, int row)
	{
		using Shape = UniformShape<std::remove_const_t<T>>;

		if constexpr (Shape::Columns > 1)
			return value[column][row];
		else if constexpr (!std::same_as<typename Shape::Scalar, std::remove_const_t<T>>)
			return value[row];
		else
			return value;
	};

	template <AUniform T>
	UniformValue ToUniformValue(const T& value)
	{
		if constexpr (std::convertible_to<const T&, std::string>)
		{
			UniformValue uniform = UniformValue::Default(UniformType::Sampler2D);
			uniform.texture = value;

			return uniform;
		}
		else
		{
			using Shape = UniformShape<T>;
			UniformValue uniform = UniformValue::Default(*UniformTypeOf<T>());

			for (int column = 0; column < Shape::Columns; ++column)
				for (int row = 0; row < Shape::Rows; ++row)
					uniform.components[column * Shape::Rows + row] = (double)UniformComponent(value, column, row);

			return uniform;
		};
	};

	template <AUniform T>
	T FromUniformValue(const UniformValue& uniform)
	{
		if constexpr (std::convertible_to<const T&, std::string>)
			return T(uniform.texture);
		else
		{
			using Shape = UniformShape<T>;
			T value{ };

			for (int column = 0; column < Shape::Columns; ++column)
				for (int row = 0; row < Shape::Rows; ++row)
					UniformComponent(value, column, row) = (typename Shape::Scalar)uniform.components[column * Shape::Rows + row];

			return value;
		};
	};

	// A value as a variable of the type holds it: what the renderer takes from
	// it, which is also what saves and reads back.
	inline UniformValue HeldAs(UniformValue uniform, UniformType type)
	{
		const UniformTypeInfo& info = InfoOf(type);

		for (int i = 0; i < info.columns * info.rows; ++i)
			switch (info.kind)
			{
			case UniformKind::Int:	uniform.components[i] = (int32_t)uniform.components[i];				break;
			case UniformKind::UInt:	uniform.components[i] = (uint32_t)(int64_t)uniform.components[i];	break;
			case UniformKind::Bool:	uniform.components[i] = uniform.components[i] != 0.0;				break;
			default:																					break;
			};

		uniform.type = type;

		return uniform;
	};

	// A variable's value, read as whatever it is assigned to: zero, the
	// identity or no image when it has none.
	struct UniformRead
	{
		std::optional<UniformValue> value;

		template <AUniform T>
		operator T() const
		{
			if (value.has_value())
				return FromUniformValue<T>(*value);

			if constexpr (std::convertible_to<const T&, std::string>)
				return T{ };
			else
				return FromUniformValue<T>(UniformValue::Default(*UniformTypeOf<T>()));
		};
	};

	#pragma endregion
};
