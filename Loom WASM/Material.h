#pragma once

#include "Loom API.h"

#include "Asset.h"
#include "Component.h"
#include "Shaders.h"
#include "ShaderVariables.h"

#include <functional>
#include <string>


namespace Loom
{
	enum class CullFace;

	struct LOOM_API Material : Component<Material, "Rendering">
	{
		#pragma region Shader

		// Names the shader to compile. Read by OnAttach, so a script sets it on
		// the material it just attached. Changed after that, it takes effect the
		// next time the material is attached, which for a scene means reloading,
		// or, for a material with no shader, once the path has been compiled for
		// another material or by ReloadChangedShaders.
		void SetShaderPath(const std::string& path) { m_shader_path = path; };
		const std::string& GetShaderPath() const { return m_shader_path; };

		// Names the shader and compiles it now, for a material already attached.
		// The editor's way, on the thread that owns the renderer.
		void ChangeShader(const std::string& path);

		// Compiles the shader named above, unless whoever built this material has
		// already put one here.
		void OnAttach() override;

		// Takes up the shader named above if this material has none and it has
		// since compiled, as a fixed file does under ReloadChangedShaders. Reads
		// nothing from disk, so it is cheap enough for every draw.
		void PickUpShader();

		// Recompiles every material's shader whose file has been written since,
		// for after something outside the editor edits one, and compiles again
		// any that failed to once its file has been written.
		static void ReloadChangedShaders();

		// Compiles source in place of the file at path, from now on, into
		// whatever reads it: a shader, a pixel shader or a PreVertex's code.
		// Returns why it does not compile, or nothing.
		static std::string EditShader(const std::string& path, const std::string& source);

		// The shader compiled from path that every Material and PixelShader
		// naming it shares, compiled now if it has not been, or null when it
		// does not compile.
		static Shader* SharedShader(const std::string& path);

		// The same, without compiling, so cheap enough for every draw.
		static Shader* CompiledShader(const std::string& path);

		// Recompiles every material's shader whose path covers, and compiles
		// again any of those that failed to, for after what goes into them
		// changes, as a PreVertex's code does.
		static void ReloadShaders(const std::function<bool(const std::string&)>& covers);

		Shader* shader = nullptr;

		#pragma endregion

		#pragma region Rendering

		void OnRender() override
		{ };

		// Hands the material's values to its program: u_color, whether it is
		// shadowed, how its textures are sampled, and the shader's variables,
		// the per-instance ones from instance. With them goes u_time, Time's
		// SinceStart.
		void Apply(uint32_t program, const UniformValues* instance = nullptr) const;

		#pragma endregion

		#pragma region Editor

		// A link that opens the shader file, its settings, the values of its
		// per-material variables, and the declarations themselves. The settings
		// and declarations are edited in the file.
		void OnGui() override;

		void OnFieldChanged(const SerializedField& field) override;

		#pragma endregion

	private:
		// Relative to the project folder, which is where the editor works from.
		Serial<Asset<Shader::extension>, "Shader"> m_shader_path;

	public:
		#pragma region Values

		// Declared after the path, which fixes its position in scene files.
		Serial<glm::vec3> color = glm::vec3(1.0f, 1.0f, 1.0f);

		// Off, what is drawn with it neither casts shadows nor has them cast on
		// it, whatever its shader's settings say.
		Serial<bool> shadows = true;

		// The per-material variables' values.
		Serial<UniformValues> variables;

		// How its textures are sampled: as its shader's Pixelated setting says,
		// blended between texels, or nearest, each texel a hard-edged square.
		enum Sampling : uint32_t
		{
			AsShader,
			Blended,
			Nearest,
		};

		// After variables, so the fields before it keep their places in scenes
		// saved without it.
		Serial<Sampling> sampling = AsShader;

		// Which faces it draws: both, or only those facing the camera (Front)
		// or away from it (Back). Light passes through the faces an open mesh
		// hides; a closed mesh casts from its far side whichever it is.
		enum Sides : uint32_t
		{
			Both,
			Front,
			Back,
		};

		// Last, for the same reason.
		Serial<Sides> sides = Both;

		// What the renderer culls for sides, drawn with a world matrix that
		// mirrors or not.
		CullFace Culled(bool mirrored) const;

		#pragma endregion

	private:
		#pragma region Editor

		void DrawSettings();
		void DrawDeclarations();

		// After the shader file has been rewritten, or failed to be, with why
		// in error.
		void Edited(bool written, const std::string& error);

		// The declaration being typed into the table's last row.
		ShaderVariable m_new_variable;

		// Why the last edit to the shader file failed, shown until the next one.
		std::string m_edit_error;

		#pragma endregion
	};
};
