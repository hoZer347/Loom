#include "ProjectTemplate.h"

// The editor only runs on Windows, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "ProjectAssets.h"

#include "Guid.h"
#include "SceneSerializer.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

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

		const char* const loomproject = "name={NAME}\n";

		// Appended to a .loomproject, whether New Project just wrote it or it
		// predates the project having any scripts.
		const char* const scripts_entries = R"(scripts=Scripts/{NAME}Scripts.vcxproj
library=Build/{NAME}Scripts.dll
)";

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
A Serial of an enum type is a dropdown of its enumerators.
SpinningTriangle is there as a worked example.
)";
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
		std::filesystem::create_directories(root / "Assets", code);
		std::filesystem::create_directories(root / build_folder, code);

		if (code)
		{
			if (error)
				*error = "Could not create " + root.string() + ": " + code.message();

			return "";
		};

		const auto fill =
			[&](const char* text)
			{
				std::string filled = ProjectAssets::Replace(text, "{NAME}", name);
				filled = ProjectAssets::Replace(filled, "{SCENE_EXTENSION}", SceneSerializer::extension);
				filled = ProjectAssets::Replace(filled, "{UNDERLINE}", std::string(name.size(), '='));
				filled = ProjectAssets::Replace(filled, "{SCENE_GUID}", Guid::New().ToString());
				filled = ProjectAssets::Replace(filled, "{ROOT_GUID}", Guid::New().ToString());
				filled = ProjectAssets::Replace(filled, "{TRIANGLE_GUID}", Guid::New().ToString());
				filled = ProjectAssets::Replace(filled, "{SCRIPT_GUID}", Guid::New().ToString());

				return filled;
			};

		const bool written =
			Write(root / (name + extension), fill(loomproject), error) &&
			Write(root / shader_path, ProjectAssets::defaultShader, error) &&
			Write(root / "Scenes" / (std::string("Main") + SceneSerializer::extension), fill(starter_scene), error) &&
			Write(root / "README.txt", fill(readme), error) &&
			Write(root / "Scripts" / "SpinningTriangle.hpp", fill(script), error) &&
			!AddScripts(root.string(), name, error).empty();

		if (!written)
			return "";

		std::cout << "Created project " << root.lexically_normal().string() << std::endl;

		return (root / (name + extension)).string();
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
		const std::string usable = UsableName(name);

		return (std::filesystem::path(folder) / "Scripts" / (usable + "Scripts.vcxproj")).lexically_normal().string();
	};

	std::string ProjectTemplate::AddScripts(
		const std::string& folder,
		const std::string& name,
		std::string* error)
	{
		const std::filesystem::path root = std::filesystem::absolute(folder).lexically_normal();
		const std::string usable = UsableName(name);
		const std::filesystem::path project = ScriptsProject(root.string(), usable);
		const std::string project_guid = ProjectGuid();
		const std::string engine = EngineRoot();

		const auto fill =
			[&](const char* text)
			{
				std::string filled = ProjectAssets::Replace(text, "{NAME}", usable);
				filled = ProjectAssets::Replace(filled, "{ENGINE}", engine);
				filled = ProjectAssets::Replace(filled, "{PROJECT}", root.string());
				filled = ProjectAssets::Replace(filled, "{PROJECT_GUID}", project_guid);

				return filled;
			};

		std::error_code code;

		// One left behind by a .loomproject that lost its entries is taken up
		// again rather than written over.
		if (!std::filesystem::exists(project, code))
		{
			const std::filesystem::path scripts = project.parent_path();

			const bool written =
				Write(project, fill(vcxproj), error) &&
				Write(scripts / (usable + "Scripts.vcxproj.filters"), fill(filters), error) &&
				Write(scripts / (usable + "Scripts.sln"), fill(solution), error);

			if (!written)
				return "";
		};

		std::filesystem::path project_file;

		for (const auto& entry : std::filesystem::directory_iterator(root, code))
			if (entry.is_regular_file(code) && entry.path().extension() == extension)
			{
				project_file = entry.path();
				break;
			};

		std::string text;

		if (project_file.empty())
		{
			project_file = root / (usable + extension);
			text = ProjectAssets::Replace(loomproject, "{NAME}", usable);
		}
		else
		{
			std::ifstream in(project_file, std::ios::binary);

			if (!in)
			{
				if (error)
					*error = "Could not read " + project_file.string();

				return "";
			};

			text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

			if (!text.empty() && text.back() != '\n')
				text += '\n';
		};

		// Already pointed here by an earlier call whose script was never made.
		if (text.find(fill(scripts_entries)) != std::string::npos)
			return project.string();

		if (!Write(project_file, text + fill(scripts_entries), error))
			return "";

		return project.string();
	};
};

#endif
