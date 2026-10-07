#include "ProjectTemplate.h"

// The editor only runs on Windows, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "ProjectAssets.h"

#include "Guid.h"
#include "SceneSerializer.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


namespace Loom
{
	namespace
	{
		bool Write(const std::filesystem::path& path, const std::string& text, std::string* error)
		{
			std::error_code code;
			std::filesystem::create_directories(path.parent_path(), code);

			std::ofstream out(path, std::ios::binary);

			if (!out)
			{
				if (error)
					*error = "Could not write " + path.string();

				return false;
			};

			out << text;

			return true;
		};

		// A guid in the shape MSBuild wants: braced and upper case.
		std::string ProjectGuid()
		{
			std::string text = Guid::New().ToString();

			for (char& c : text)
				c = (char)toupper((unsigned char)c);

			return '{' + text + '}';
		};

		const char* const loomproject = R"(name={NAME}
)";

		const char* const scripts_entries = R"(scripts=Scripts/{NAME}Scripts.vcxproj
library=Build/{NAME}Scripts.dll
)";

		// Every scripts project the template writes defines this, wherever it
		// has been moved to since.
		constexpr const char* script_module_define = "LOOM_SCRIPT_MODULE";

		const char* const script = R"(#pragma once

#include "Component.h"
#include "Material.h"
#include "Mesh.h"

#include <cmath>
#include <numbers>
#include <string>


namespace {NAME}Scripts
{
	// A script is an ordinary Loom component, written whole in one header.
	// Deriving from Loom::Component is all it takes to appear under Add
	// Component. Serial members show up in the inspector and are written to the
	// scene file, with no editor code to write for them.
	struct SpinningTriangle : Loom::Component<SpinningTriangle>
	{
		void OnAttach() override
		{
			Loom::Material* material = m_gameObject->GetComponent<Loom::Material>();

			if (material == nullptr)
				material = m_gameObject->Attach<Loom::Material>();

			// Named, not built here: the engine compiles one shader per path and
			// shares it, so a script hands over the name and lets it do that.
			material->SetShaderPath(m_shader);

			m_mesh = m_gameObject->GetComponent<Loom::Mesh>();

			if (m_mesh == nullptr)
				m_mesh = m_gameObject->Attach<Loom::Mesh>();

			m_mesh->material = material;

			Rebuild();
		};

		void OnUpdate() override
		{
			if (!m_spinning)
				return;

			m_angle += m_speed;

			Rebuild();
		};

	private:
		static constexpr int corners = 3;

		void Rebuild()
		{
			if (m_mesh == nullptr)
				return;

			m_mesh->m_vertices->clear();

			for (int corner = 0; corner < corners; corner++)
			{
				const float turn = m_angle + (float)corner * 2.0f * std::numbers::pi_v<float> / corners;

				m_mesh->m_vertices->push_back(std::cos(turn) * m_radius);
				m_mesh->m_vertices->push_back(std::sin(turn) * m_radius);
				m_mesh->m_vertices->push_back(0.0f);
			};
		};

		Loom::Serial<float> m_speed = 0.02f;
		Loom::Serial<float> m_radius = 0.6f;
		Loom::Serial<float> m_angle;
		Loom::Serial<bool> m_spinning = true;
		Loom::Serial<std::string> m_shader = "Assets/Shader.shader";

		Loom::Mesh* m_mesh = nullptr;
	};
};
)";

		const char* const vcxproj = R"(<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64">
      <Configuration>Debug</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64">
      <Configuration>Release</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>18.0</VCProjectVersion>
    <Keyword>Win32Proj</Keyword>
    <ProjectGuid>{PROJECT_GUID}</ProjectGuid>
    <RootNamespace>{NAME}Scripts</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
    <ProjectName>{NAME}Scripts</ProjectName>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'$(Configuration)'=='Debug'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>true</UseDebugLibraries>
    <PlatformToolset>v145</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'=='Release'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset>v145</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup>
    <OutDir>{PROJECT}\Build\</OutDir>
    <IntDir>{PROJECT}\Build\Intermediate\$(Configuration)\</IntDir>
    <TargetName>{NAME}Scripts</TargetName>
  </PropertyGroup>
  <!-- Here rather than in a .vcxproj.user, which is per-user and is not
       written out with the project: these are what F5 does on a fresh
       checkout, and the Debugging property page shadows them. -->
  <PropertyGroup>
    <DebuggerFlavor>WindowsLocalDebugger</DebuggerFlavor>
    <LocalDebuggerCommand>{ENGINE}\x64\$(Configuration)\Loom Editor.exe</LocalDebuggerCommand>
    <LocalDebuggerCommandArguments>"{PROJECT}" --play</LocalDebuggerCommandArguments>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <LanguageStandard>stdcpplatest</LanguageStandard>
      <ConformanceMode>true</ConformanceMode>
      <MultiProcessorCompilation>true</MultiProcessorCompilation>
      <DisableSpecificWarnings>4251;4275</DisableSpecificWarnings>
      <PreprocessorDefinitions>LOOM_SCRIPT_MODULE;_WIN32_WINNT=0x0601;WIN32;_WINDOWS;_USRDLL;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalIncludeDirectories>{ENGINE}\Loom WASM;{ENGINE}\Loom ImGui;{ENGINE}\External Libraries\glm;{ENGINE}\External Libraries\glew\include;{ENGINE}\External Libraries\glfw\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
    <Link>
      <SubSystem>Windows</SubSystem>
      <GenerateDebugInformation>true</GenerateDebugInformation>
      <AdditionalLibraryDirectories>{ENGINE}\x64\$(Configuration);{ENGINE}\External Libraries\glew\lib\Release\x64;{ENGINE}\External Libraries\glfw\lib-vc2022;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalDependencies>Loom Editor.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Debug'">
    <ClCompile>
      <PreprocessorDefinitions>_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
    <ClCompile>
      <PreprocessorDefinitions>NDEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <FunctionLevelLinking>true</FunctionLevelLinking>
      <IntrinsicFunctions>true</IntrinsicFunctions>
    </ClCompile>
  </ItemDefinitionGroup>
  <!-- Each component is a header, compiled on its own so that a new one is
       built (and registers itself) without being listed here. -->
  <ItemGroup>
    <ClCompile Include="**\*.hpp">
      <CompileAs>CompileAsCpp</CompileAs>
    </ClCompile>
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
)";

		const char* const filters = R"(<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
    <ClCompile Include="**\*.hpp" />
  </ItemGroup>
</Project>
)";

		const char* const solution = R"(
Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 18
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "{NAME}Scripts", "{NAME}Scripts.vcxproj", "{PROJECT_GUID}"
EndProject
Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
		Debug|x64 = Debug|x64
		Release|x64 = Release|x64
	EndGlobalSection
	GlobalSection(ProjectConfigurationPlatforms) = postSolution
		{PROJECT_GUID}.Debug|x64.ActiveCfg = Debug|x64
		{PROJECT_GUID}.Debug|x64.Build.0 = Debug|x64
		{PROJECT_GUID}.Release|x64.ActiveCfg = Release|x64
		{PROJECT_GUID}.Release|x64.Build.0 = Release|x64
	EndGlobalSection
	GlobalSection(SolutionProperties) = preSolution
		HideSolutionNode = FALSE
	EndGlobalSection
EndGlobal
)";

		// The tabs and newlines are the scene format's syntax, so they are spelled
		// out rather than left to whatever a raw string would preserve.
		const char* const starter_scene =
			"Main({SCENE_GUID}):\n"
			"\tGameObject Root({ROOT_GUID})\n"
			"\t\t0 = 0\n"
			"\t\t1 = false\n"
			"\t\tGameObject Triangle({TRIANGLE_GUID})\n"
			"\t\t\t0 = 0\n"
			"\t\t\t1 = false\n"
			"\t\t\tComponent SpinningTriangle({SCRIPT_GUID})\n"
			"\t\t\t\t0 = 0.02\n"
			"\t\t\t\t1 = 0.6\n"
			"\t\t\t\t2 = 0\n"
			"\t\t\t\t3 = true\n"
			"\t\t\t\t4 = \"Assets/Shader.shader\"\n";

		const char* const readme = R"({NAME}
{UNDERLINE}

A Loom project. The editor opens the folder; the scenes are in Scenes/ and the
scripts are an ordinary C++ project in Scripts/.

    Scenes/Main{SCENE_EXTENSION}     opened when the project loads
    Scripts/{NAME}Scripts.sln    open this in Visual Studio to write scripts
    Assets/Shader.shader         what the starter scene draws with
    Build/                       where the compiled script library lands

Scripts are Loom components written in headers: add a .hpp to Scripts/ with

    struct Name : Loom::Component<Name> { ... };

in it and it is compiled and offered under Add Component, with nothing to
register. One header can hold several. With the engine's Loom Visual Studio
extension installed (Loom Visual Studio/install.ps1), right-click in Solution
Explorer and pick Add > Loom Script... to write one there.

Each header is compiled on its own and they may include one another, so
anything defined outside a struct has to be inline. Compile them from the
editor (the Compile button in the Project panel, or just press Play) or build
the solution in Visual Studio. They are the same build. The library links
the editor's import library, so there is one engine in the process rather
than one per module.

Start Debugging (F5) builds the scripts and then starts the editor on this
project with the scene running, so breakpoints in them are hit.

Members declared as Loom::Serial<type> name = default; appear in the inspector
under their own name (m_speed shows as "Speed") and are written into the scene
file, in the order they were declared - which is also how the file names them,
so inserting one in the middle shifts the values already saved.
SpinningTriangle is there as a worked example.
)";

		std::string Fill(
			const char* text,
			const std::string& name,
			const std::string& project,
			const std::string& project_guid)
		{
			std::string filled = ProjectAssets::Replace(text, "{NAME}", name);
			filled = ProjectAssets::Replace(filled, "{ENGINE}", ProjectTemplate::EngineRoot());
			filled = ProjectAssets::Replace(filled, "{PROJECT}", project);
			filled = ProjectAssets::Replace(filled, "{PROJECT_GUID}", project_guid);
			filled = ProjectAssets::Replace(filled, "{SCENE_EXTENSION}", SceneSerializer::extension);
			filled = ProjectAssets::Replace(filled, "{UNDERLINE}", std::string(name.size(), '='));
			filled = ProjectAssets::Replace(filled, "{SCENE_GUID}", Guid::New().ToString());
			filled = ProjectAssets::Replace(filled, "{ROOT_GUID}", Guid::New().ToString());
			filled = ProjectAssets::Replace(filled, "{TRIANGLE_GUID}", Guid::New().ToString());
			filled = ProjectAssets::Replace(filled, "{SCRIPT_GUID}", Guid::New().ToString());

			return filled;
		};

		// The scripts project, its solution and the example script, under
		// root's scripts folder.
		bool WriteScripts(const std::filesystem::path& root, const std::string& name, std::string* error)
		{
			const std::string project = root.lexically_normal().string();
			const std::string project_guid = ProjectGuid();
			const std::filesystem::path folder = root / ProjectTemplate::scripts_folder;

			const auto fill =
				[&](const char* text)
				{
					return Fill(text, name, project, project_guid);
				};

			return
				Write(folder / "SpinningTriangle.hpp", fill(script), error) &&
				Write(folder / (name + "Scripts.vcxproj"), fill(vcxproj), error) &&
				Write(folder / (name + "Scripts.vcxproj.filters"), fill(filters), error) &&
				Write(folder / (name + "Scripts.sln"), fill(solution), error);
		};

		// Relative to root when it can be, which keeps a project file working
		// wherever the project is moved, and whole when it cannot.
		std::string PathIn(const std::filesystem::path& root, const std::filesystem::path& path)
		{
			const std::filesystem::path relative = path.lexically_normal().lexically_relative(root);

			return relative.empty()
				? path.lexically_normal().generic_string()
				: relative.generic_string();
		};

		// Where the build of a scripts project puts its library: its OutDir,
		// which MSBuild reads against the project's own folder, unless that is
		// made of macros there is no working out here.
		std::filesystem::path LibraryOf(const std::filesystem::path& root, const std::filesystem::path& scripts)
		{
			const std::string text = ProjectAssets::ReadText(scripts.string());
			const std::string target = ProjectAssets::Element(text, "TargetName");
			const std::string out = ProjectAssets::Element(text, "OutDir");

			const std::filesystem::path folder = out.empty() || out.find("$(") != std::string::npos
				? root / ProjectTemplate::build_folder
				: scripts.parent_path() / out;

			return folder / ((target.empty() ? scripts.stem().string() : target) + ".dll");
		};

		// Sorted first, so the same one is found every time.
		std::filesystem::path FindScriptsProject(const std::filesystem::path& folder)
		{
			std::vector<std::filesystem::path> found{ };
			std::error_code code;

			for (auto entry = std::filesystem::recursive_directory_iterator(
					folder, std::filesystem::directory_options::skip_permission_denied, code);
				entry != std::filesystem::recursive_directory_iterator();
				entry.increment(code))
				if (entry->path().extension() == ".vcxproj" &&
					ProjectAssets::ReadText(entry->path().string()).find(script_module_define) != std::string::npos)
					found.push_back(entry->path());

			std::sort(found.begin(), found.end());

			return found.empty()
				? std::filesystem::path()
				: found.front();
		};
	};

	std::string ProjectTemplate::EngineRoot()
	{
		char buffer[MAX_PATH]{ };

		GetModuleFileNameA(nullptr, buffer, MAX_PATH);

		// <engine>/x64/<configuration>/Loom Editor.exe
		std::filesystem::path root =
			std::filesystem::path(buffer).parent_path().parent_path().parent_path();

		std::error_code code;

		if (std::filesystem::exists(root / "Loom WASM" / "Loom.h", code))
			return root.lexically_normal().string();

		return std::filesystem::current_path(code).string();
	};

	bool ProjectTemplate::IsValidName(const std::string& name)
	{
		if (name.empty() || name.size() > maxNameLength)
			return false;

		for (const char c : name)
			if (!isalnum((unsigned char)c) && c != '_' && c != '-')
				return false;

		return !isdigit((unsigned char)name.front());
	};

	std::string ProjectTemplate::Create(
		const std::string& folder,
		const std::string& name,
		std::string* error)
	{
		if (!IsValidName(name))
		{
			if (error)
				*error = "'" + name + "' is not a usable project name (letters, digits, _ and - only)";

			return "";
		};

		const std::filesystem::path root = std::filesystem::path(folder) / name;

		std::error_code code;

		if (std::filesystem::exists(root / (name + extension), code))
		{
			if (error)
				*error = "There is already a project in " + root.string();

			return "";
		};

		std::filesystem::create_directories(root / "Scenes", code);
		std::filesystem::create_directories(root / scripts_folder, code);
		std::filesystem::create_directories(root / "Assets", code);
		std::filesystem::create_directories(root / build_folder, code);

		if (code)
		{
			if (error)
				*error = "Could not create " + root.string() + ": " + code.message();

			return "";
		};

		const std::string project = root.lexically_normal().string();

		const auto fill =
			[&](const char* text)
			{
				return Fill(text, name, project, "");
			};

		const bool written =
			Write(root / (name + extension), fill(loomproject) + fill(scripts_entries), error) &&
			Write(root / shader_path, ProjectAssets::defaultShader, error) &&
			Write(root / "Scenes" / (std::string("Main") + SceneSerializer::extension), fill(starter_scene), error) &&
			Write(root / "README.txt", fill(readme), error) &&
			WriteScripts(root, name, error);

		if (!written)
			return "";

		std::cout << "Created project " << project << std::endl;

		return (root / (name + extension)).string();
	};

	std::string ProjectTemplate::ValueOf(const std::string& line, const char* key)
	{
		const std::string prefix = std::string(key) + '=';

		return line.rfind(prefix, 0) == 0
			? line.substr(prefix.size())
			: "";
	};

	std::string ProjectTemplate::EnsureScripts(const std::string& project_file, std::string* error)
	{
		std::error_code code;

		const std::filesystem::path file = std::filesystem::absolute(project_file, code).lexically_normal();
		const std::filesystem::path root = file.parent_path();

		std::ifstream in(file);

		if (!in)
		{
			if (error)
				*error = "Could not read " + file.string();

			return "";
		};

		// Everything but the scripts entries, which are written again below.
		std::string kept, line, name, named;

		while (std::getline(in, line))
		{
			while (!line.empty() && line.back() == '\r')
				line.pop_back();

			if (const std::string value = ValueOf(line, scripts_key); !value.empty())
				named = value;
			else if (ValueOf(line, library_key).empty())
				kept += line + '\n';

			if (const std::string value = ValueOf(line, name_key); !value.empty())
				name = value;
		};

		in.close();

		if (!named.empty())
		{
			const std::filesystem::path scripts = (root / named).lexically_normal();

			if (std::filesystem::is_regular_file(scripts, code))
				return scripts.string();
		};

		std::filesystem::path scripts = FindScriptsProject(root);

		if (!scripts.empty())
		{
			kept += std::string(scripts_key) + '=' + PathIn(root, scripts) + '\n';
			kept += std::string(library_key) + '=' + PathIn(root, LibraryOf(root, scripts)) + '\n';

			std::cout << "Found the scripts project " << scripts.string() << std::endl;
		}
		else
		{
			name = UsableName(name.empty() ? file.stem().string() : name);

			scripts = ScriptsProject(root.string(), name);

			if (std::filesystem::exists(scripts, code))
			{
				if (error)
					*error = scripts.string() + " is in the way, and is not a Loom scripts project";

				return "";
			};

			if (!WriteScripts(root, name, error))
				return "";

			kept += Fill(scripts_entries, name, root.string(), "");

			std::cout << "Created the scripts project " << scripts.string() << std::endl;
		};

		if (!Write(file, kept, error))
			return "";

		return scripts.lexically_normal().string();
	};

	std::string ProjectTemplate::UsableName(const std::string& name)
	{
		constexpr const char* fallback = "Project";

		std::string usable;

		for (const char c : name)
			if (isalnum((unsigned char)c) || c == '_' || c == '-')
				usable += c;

		usable = usable.substr(0, maxNameLength);

		return IsValidName(usable)
			? usable
			: fallback + usable.substr(0, maxNameLength - strlen(fallback));
	};

	std::string ProjectTemplate::ScriptsProject(const std::string& folder, const std::string& name)
	{
		return (std::filesystem::path(folder) / scripts_folder / (UsableName(name) + "Scripts.vcxproj")).lexically_normal().string();
	};

	std::string ProjectTemplate::AddScripts(
		const std::string& folder,
		const std::string& name,
		std::string* error)
	{
		const std::filesystem::path root = std::filesystem::absolute(folder).lexically_normal();

		std::error_code code;
		std::filesystem::path project_file;

		for (const auto& entry : std::filesystem::directory_iterator(root, code))
			if (entry.is_regular_file(code) && entry.path().extension() == extension)
			{
				project_file = entry.path();
				break;
			};

		if (project_file.empty())
		{
			const std::string usable = UsableName(name);

			project_file = root / (usable + extension);

			if (!Write(project_file, Fill(loomproject, usable, root.string(), ""), error))
				return "";
		};

		return EnsureScripts(project_file.string(), error);
	};
};

#endif
