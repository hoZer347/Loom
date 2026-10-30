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
	*   editor's Play runs first
	*/
	struct ProjectTemplate final
	{
		#pragma region Project

		static constexpr const char* extension = ".loomproject";

		// What the starter scene draws with, relative to the project folder.
		static constexpr const char* shader_path = "Assets/Lit.shader";

		// Where the scripts build to, intermediates and all.
		static constexpr const char* build_folder = "Build";

		static constexpr const char* scripts_folder = "Scripts";

		// Where a project's scenes are, and where a scene saved for the first
		// time goes.
		static constexpr const char* scenes_folder = "Scenes";

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

		#pragma endregion

		#pragma region Scripts

		// Gives a project file a scripts project when it names none that is
		// there: the first one under the project's folder, or failing that a
		// new one from the template, and writes it into the file. Returns the
		// .vcxproj, or an empty string with the reason in error.
		static std::string EnsureScripts(const std::string& project_file, std::string* error = nullptr);

		// Gives a scripts project the template wrote before the engine's
		// header was precompiled the settings that precompile it, naming the
		// engine's header folder the way the project already does. One that
		// has them, or has been changed past finding where they go, is left
		// alone. False, with the reason in error, when it could not be written.
		static bool Precompile(const std::string& scripts, std::string* error = nullptr);

		// Gives a scripts project from before Tests ran its cases the
		// exception handling that unwinds a crashed case's locals. One that
		// sets its own is left alone.
		static bool CatchCrashes(const std::string& scripts, std::string* error = nullptr);

		// Gives a scripts project a Directory.Build.props that marks it
		// read-only, which is what keeps Visual Studio from warning about its
		// wildcard. A folder with one of its own is left alone.
		static bool ReadOnly(const std::string& scripts, std::string* error = nullptr);

		// EnsureScripts for the project in folder, writing it a .loomproject
		// named after name first if it has none. Returns the .vcxproj, or an
		// empty string with the reason in error.
		static std::string AddScripts(
			const std::string& folder,
			const std::string& name,
			std::string* error = nullptr);

		// The .vcxproj AddScripts writes for the project in folder.
		static std::string ScriptsProject(const std::string& folder, const std::string& name);

		// A line of a scripts build as the editor shows it: without the
		// indent and the .vcxproj MSBuild ends it with.
		static std::string BuildLine(const std::string& line);

		// The command that builds a scripts project, showing only its errors
		// and warnings. A project under the temporary folder builds quietly
		// there too.
		static std::string BuildCommand(const std::string& msbuild, const std::string& project, const std::string& configuration, bool rebuild);

		#pragma endregion

		#pragma region Engine

		// Where the engine lives, worked out from the running executable
		// (<engine>/x64/<configuration>/Loom Editor.exe).
		static std::string EngineRoot();

		// Whether the engine at root is a git worktree beside its main
		// checkout, such as an agent's slot, whose .git is a file naming it.
		static bool IsWorktree(const std::string& root);

		#pragma endregion

		#pragma region Names

		static constexpr size_t maxNameLength = 64;

		// A name a file system and MSBuild will both accept.
		static bool IsValidName(const std::string& name);

		// The name with what IsValidName refuses taken out.
		static std::string UsableName(const std::string& name);

		#pragma endregion
	};
};
