#pragma once

#include <string>


namespace Loom
{
	/**
	* Loom::ProjectAssets
	* - Writes the new files the Project panel's Create menu offers, each one
	*   something the engine or the scripts build can use straight away
	* - A script is one header the scripts project compiles by its glob, so the
	*   next compile puts it in Add Component
	*/
	struct ProjectAssets final
	{
		static constexpr const char* shaderExtension = ".shader";
		static constexpr const char* textureExtension = ".png";

		// The shader every new one starts from: a flat colour.
		static const char* const defaultShader;

		// Each returns the path written, or an empty string with the reason in
		// error.
		static std::string CreateFolder(const std::string& folder, const std::string& name, std::string* error = nullptr);
		static std::string CreateShader(const std::string& folder, const std::string& name, std::string* error = nullptr);
		static std::string CreateTexture(const std::string& folder, const std::string& name, std::string* error = nullptr);

		// Writes <name>.hpp into folder, which has to sit under the scripts
		// project's own folder.
		static std::string CreateScript(
			const std::string& scripts_project,
			const std::string& folder,
			const std::string& name,
			std::string* error = nullptr);

		// A name usable as a file name, and as a path in an MSBuild project.
		static bool IsValidName(const std::string& name);

		// Why name cannot be a new script in the given project, or an empty
		// string when it can: it has to be a C++ type name that no script is
		// already using.
		static std::string ScriptNameProblem(const std::string& scripts_project, const std::string& name);

		static std::string Replace(std::string text, const std::string& token, const std::string& value);
	};
};
