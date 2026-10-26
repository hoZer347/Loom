#pragma once

#include "Shaders.h"

#include <string>
#include <string_view>
#include <vector>


namespace Loom
{
	/**
	* Loom::ProjectAssets
	* - Writes the new files the Scenes, 2D and Scripts tabs' Create menus
	*   offer, each one something the engine or the scripts build can use
	*   straight away
	* - A script is one header the scripts project compiles by its glob, so the
	*   next compile puts it in Add Component
	*/
	struct ProjectAssets final
	{
		#pragma region File Kinds

		static constexpr const char* shaderExtension = Shader::extension;
		static constexpr const char* textureExtension = ".png";
		static constexpr std::string_view scriptExtensions[] = { ".hpp", ".h", ".cpp" };

		enum struct Opener
		{
			Editor,
			Prefab,
			Shaders,
			Sprites,
			Palette,
			VisualStudio,
			Shell,
		};

		// Scenes open in the editor, prefabs in its Prefab panel, shaders in its
		// Shaders panel, textures and sprites in its Sprites panel, palettes in
		// its Palette panel, the scripts project's sources in Visual Studio,
		// and anything else through the shell.
		// scripts_folder is empty when there is no scripts project.
		static Opener OpenerOf(const std::string& path, const std::string& scripts_folder);

		// Whether name is a scene, prefab, shader, texture, sprite, palette,
		// sound or font, which the Project panel's tabs list along with
		// models. The scripts are the Scripts tab's too, and anything else in
		// the project folder (Visual Studio's files, notes) is not part of
		// the game.
		static bool IsGameFile(const std::string& name);
		static bool IsScriptFile(const std::string& name);
		static bool IsShaderFile(const std::string& name);
		static bool IsTextureFile(const std::string& name);

		// A texture, sprite or palette, which the 2D tab lists.
		static bool IsTwoDFile(const std::string& name);
		static bool IsSoundFile(const std::string& name);
		static bool IsFontFile(const std::string& name);

		#pragma endregion

		#pragma region Creating

		// What a new script header starts out as.
		enum struct ScriptKind
		{
			Component,
			State,
			StateMachine,
		};

		// A name usable as a file name, and as a path in an MSBuild project.
		static bool IsValidName(const std::string& name);

		// Each returns the path written, or an empty string with the reason in
		// error.
		static std::string CreateFolder(const std::string& folder, const std::string& name, std::string* error = nullptr);
		static std::string CreateShader(const std::string& folder, const std::string& name, std::string* error = nullptr);
		static std::string CreateTexture(const std::string& folder, const std::string& name, std::string* error = nullptr);
		static std::string CreatePalette(const std::string& folder, const std::string& name, std::string* error = nullptr);

		// Writes <name>.hpp into folder, which has to sit under the scripts
		// project's own folder.
		static std::string CreateScript(
			const std::string& scripts_project,
			const std::string& folder,
			const std::string& name,
			ScriptKind kind,
			std::string* error = nullptr);

		#pragma endregion

		#pragma region Paths

		// Copies file into folder under its own name, or with the first number
		// after its stem that nothing there is using. A file already in folder
		// stays as it is, and its own path comes back.
		static std::string CopyInto(const std::string& file, const std::string& folder, std::string* error = nullptr);

		// Moves a file or folder into folder under its own name. One already in
		// folder stays as it is, and its own path comes back. Nothing in folder
		// is replaced, and a folder never goes inside itself.
		static std::string MoveInto(const std::string& path, const std::string& folder, std::string* error = nullptr);

		// Where path is once from has moved to to: path itself when it is
		// neither from nor inside it.
		static std::string Relocate(const std::string& path, const std::string& from, const std::string& to);

		// Whether path is folder or somewhere inside it.
		static bool IsUnder(const std::string& path, const std::string& folder);

		// Where path is from folder, with / between folders, the way components
		// name the files they use.
		static std::string RelativeTo(const std::string& path, const std::string& folder);

		#pragma endregion

		#pragma region Scripts

		// Why name cannot be a new script in the given project, or an empty
		// string when it can: it has to be a C++ type name that no script,
		// registered component or registered state is already using.
		static std::string ScriptNameProblem(const std::string& scripts_project, const std::string& name);

		// The types a script's text defines, in order. A forward declaration
		// defines nothing.
		static std::vector<std::string> DeclaredTypes(const std::string& text);

		// The line, counted from one, that defines the type named name, export
		// macro and all, or zero when text does not define it outside a comment.
		// A primary template is passed over, since no component is one.
		static int DeclarationLine(const std::string& text, const std::string& name);

		struct DeclaredScript
		{
			std::string name;
			bool state = false;
		};

		// The types a script's text defines that pass themselves to their base,
		// the way Component, State and StateMachine are derived from, in order.
		// Found before a compile, so a script shows before it is built.
		static std::vector<DeclaredScript> DeclaredScripts(const std::string& text);

		#pragma endregion

		#pragma region Shaders

		// The shader every new one starts from: the mesh through the camera,
		// in the material's colour, with every engine section off.
		static const char* const defaultShader;

		// Every shader under the project folder, relative to it with / between
		// folders, in order.
		static std::vector<std::string> ShadersIn(const std::string& folder);

		// The shader a new project draws with, or else the first the project
		// folder has. The new project's for no folder.
		static std::string ProjectShader(const std::string& folder);

		#pragma endregion

		#pragma region Text

		static std::string Replace(std::string text, const std::string& token, const std::string& value);

		// The whole file, or an empty string when it cannot be read.
		static std::string ReadText(const std::string& path);

		// What the first <tag> in an MSBuild project holds, or an empty string.
		static std::string Element(const std::string& xml, const std::string& tag);

		#pragma endregion
	};
};
