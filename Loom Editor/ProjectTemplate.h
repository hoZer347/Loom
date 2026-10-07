#pragma once

#include <string>


namespace Loom
{
	/**
	* Loom::ProjectTemplate
	* - Writes out a new Loom project: a scene folder, a starter scene, and a
	*   Visual Studio C++ project for the scripts that go with it
	* - The scripts project builds a DLL against the editor's import library, so
	*   opening it in Visual Studio and pressing build is the same build the
	*   editor's Compile button runs
	*/
	struct ProjectTemplate final
	{
		static constexpr const char* extension = ".loomproject";

		// What the starter scene draws with, relative to the project folder.
		static constexpr const char* shader_path = "Assets/Shader.shader";

		// Where the scripts build to, intermediates and all.
		static constexpr const char* build_folder = "Build";

		static constexpr const char* scripts_folder = "Scripts";

		// What a project file's lines are, each key=value.
		static constexpr const char* name_key = "name";
		static constexpr const char* scripts_key = "scripts";
		static constexpr const char* library_key = "library";

		// The value of a project file line holding key, or an empty string for
		// a line holding another.
		static std::string ValueOf(const std::string& line, const char* key);

		// Creates the project under folder/name. Returns the path of the
		// .loomproject file, or an empty string with the reason in error.
		static std::string Create(
			const std::string& folder,
			const std::string& name,
			std::string* error = nullptr);

		// Gives a project file a scripts project when it names none that is
		// there: the first one under the project's folder, or failing that a
		// new one from the template, and writes it into the file. Returns the
		// .vcxproj, or an empty string with the reason in error.
		static std::string EnsureScripts(const std::string& project_file, std::string* error = nullptr);

		// Where the engine lives, worked out from the running executable
		// (<engine>/x64/<configuration>/Loom Editor.exe).
		static std::string EngineRoot();

		// EnsureScripts for the project in folder, writing it a .loomproject
		// named after name first if it has none. Returns the .vcxproj, or an
		// empty string with the reason in error.
		static std::string AddScripts(
			const std::string& folder,
			const std::string& name,
			std::string* error = nullptr);

		// The .vcxproj AddScripts writes for the project in folder.
		static std::string ScriptsProject(const std::string& folder, const std::string& name);

		static constexpr size_t maxNameLength = 64;

		// A name a file system and MSBuild will both accept.
		static bool IsValidName(const std::string& name);

		// The name with what IsValidName refuses taken out.
		static std::string UsableName(const std::string& name);
	};
};
