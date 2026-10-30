#include "doctest.h"

#include "Test Support.h"

#include "GameObject.h"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Scene.h"
#include "Shaders.h"
#include "ShaderVariables.h"
#include "Utilities/Clock.h"

#include "OpenGL.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using LoomTests::Pump;
using LoomTests::RecordingRenderer;


#pragma region Helpers

namespace
{
	// A shader file in the temp folder, gone again when the test is.
	struct ShaderFile
	{
		std::filesystem::path path;

		ShaderFile(const std::string& name, const std::string& source) :
			path(std::filesystem::temp_directory_path() / name)
		{
			Write(source);
		};

		void Write(const std::string& source) const
		{
			std::ofstream(path, std::ios::binary) << source;
		};

		~ShaderFile()
		{
			std::error_code code;
			std::filesystem::remove(path, code);
		};

		std::string Text() const
		{
			std::ifstream file(path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(file), { });
		};
	};

	const std::string STAGES =
		"// ===VERTEX===\n"
		"in vec3 aPos;\n"
		"uniform mat4 u_model;\n"
		"void main() { }\n"
		"// ===FRAGMENT===\n"
		"void main() { }\n";
};

#pragma endregion


TEST_SUITE("Shader variables")
{
	#pragma region Variables

	TEST_CASE("every uniform a shader declares is a variable, bar the engine's")
	{
		const std::vector<Loom::ShaderVariable> variables = Loom::ParseShaderVariables(
			"// ===COMMON===\n"
			"uniform float u_speed;\n"
			"uniform highp vec3 u_tint; // instance\n"
			"uniform mat3x3 u_basis; // material\n"
			"uniform sampler2D u_albedo;\r\n"
			"uniform highp sampler2DShadow u_custom;\n"
			"// ===VERTEX===\n"
			"uniform mat4 u_model;\n"
			"uniform mat4 u_viewProjection;\n"
			"uniform float u_speed;\n"
			"uniform float u_time;\n"
			"uniform float u_seed;\n"
			"// ===FRAGMENT===\n"
			"uniform vec3 u_color;\n"
			"// uniform float u_commented;\n");

		REQUIRE(variables.size() == 4);

		CHECK(variables[0].name == "u_speed");
		CHECK(variables[0].type == Loom::UniformType::Float);
		CHECK(variables[0].scope == Loom::UniformScope::Material);

		CHECK(variables[1].name == "u_tint");
		CHECK(variables[1].type == Loom::UniformType::Vec3);
		CHECK(variables[1].scope == Loom::UniformScope::Instance);

		CHECK(variables[2].name == "u_basis");
		CHECK(variables[2].type == Loom::UniformType::Mat3);

		CHECK(variables[3].name == "u_albedo");
		CHECK(variables[3].type == Loom::UniformType::Sampler2D);
	};

	TEST_CASE("every type in the table is found by its own spelling")
	{
		for (const Loom::UniformTypeInfo& info : Loom::UNIFORM_TYPES)
		{
			Loom::UniformType type = Loom::UniformType::Float;

			CHECK(Loom::FindUniformType(info.glsl, type));
			CHECK(type == info.type);
		};
	};

	TEST_CASE("a matrix defaults to the identity and everything else to zero")
	{
		const Loom::UniformValue matrix = Loom::UniformValue::Default(Loom::UniformType::Mat2x3);

		// Two columns of three.
		CHECK(matrix.components[0] == 1.0);
		CHECK(matrix.components[1] == 0.0);
		CHECK(matrix.components[2] == 0.0);
		CHECK(matrix.components[3] == 0.0);
		CHECK(matrix.components[4] == 1.0);
		CHECK(matrix.components[5] == 0.0);

		const Loom::UniformValue vector = Loom::UniformValue::Default(Loom::UniformType::Vec4);

		for (double component : vector.components)
			CHECK(component == 0.0);
	};

	TEST_CASE("values are written and read back as they were")
	{
		Loom::UniformValues written;
		written.Set("u_speed", Loom::UniformType::Float, { 0.1 });
		written.Set("u_offset", Loom::UniformType::IVec2, { -3.0, 7.0 });
		written.Set("u_mask", Loom::UniformType::UInt, { 4294967295.0 });
		written.Set("u_flags", Loom::UniformType::BVec3, { 1.0, 0.0, 1.0 });
		written.Set("u_basis", Loom::UniformType::Mat2, { 1.0, 2.0, 3.0, 4.0 });
		written.SetTexture("u_albedo", "Assets/With Spaces/brick.png");

		Loom::UniformValues read;
		read.Read(written.Write());

		REQUIRE(read.values.size() == written.values.size());

		CHECK((float)read.values["u_speed"].components[0] == 0.1f);
		CHECK(read.values["u_offset"].components[0] == -3.0);
		CHECK(read.values["u_offset"].components[1] == 7.0);
		CHECK(read.values["u_mask"].components[0] == 4294967295.0);
		CHECK(read.values["u_flags"].components[2] == 1.0);
		CHECK(read.values["u_basis"].components[3] == 4.0);
		CHECK(read.values["u_albedo"].type == Loom::UniformType::Sampler2D);
		CHECK(read.values["u_albedo"].texture == "Assets/With Spaces/brick.png");
	};

	TEST_CASE("a value of another type than the variable's is not its value")
	{
		Loom::UniformValues values;
		values.Set("u_tint", Loom::UniformType::Vec3, { 1.0, 0.5, 0.25 });

		CHECK(values.Find({ "u_tint", Loom::UniformType::Vec3 }) != nullptr);
		CHECK(values.Find({ "u_tint", Loom::UniformType::Vec4 }) == nullptr);
		CHECK(values.Find({ "u_other", Loom::UniformType::Vec3 }) == nullptr);
	};

	#pragma endregion

	#pragma region Settings in Files

	TEST_CASE("writing the settings into a file without them adds the section in front of the stages")
	{
		const ShaderFile file("settings without section.shader", "// A header comment.\n\n" + STAGES);

		std::string error;
		REQUIRE(Loom::WriteShaderSettings(file.path.string(), { .shading = true }, error));

		CHECK(file.Text() == "// A header comment.\n\n// ===SETTINGS===\nshading = on\nshadows = off\ntransparency = off\npixelated = off\n\n" + STAGES);

		const Loom::ShaderSettings read = Loom::ParseShaderSettings(file.Text());

		CHECK(read.shading);
		CHECK_FALSE(read.shadows);
	};

	TEST_CASE("writing the settings rewrites their lines in place, keeping the file's comments and line endings")
	{
		const ShaderFile file(
			"settings in place.shader",
			"// ===SETTINGS===\r\n"
			"// Lit, for now.\r\n"
			"shading = on\r\n"
			"\r\n"
			"// ===FRAGMENT===\r\n"
			"void main() { }\r\n");

		std::string error;
		REQUIRE(Loom::WriteShaderSettings(file.path.string(), { .shading = false, .shadows = true, .transparency = true }, error));

		CHECK(file.Text() ==
			"// ===SETTINGS===\r\n"
			"// Lit, for now.\r\n"
			"shading = off\r\n"
			"shadows = on\r\n"
			"transparency = on\r\n"
			"pixelated = off\r\n"
			"\r\n"
			"// ===FRAGMENT===\r\n"
			"void main() { }\r\n");
	};

	#pragma endregion

	#pragma region Reloading

#ifndef __EMSCRIPTEN__
	// Native only, for the reason the Shader suite gives: the web build fetches
	// shader files over HTTP, which needs a browser.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader reloads its file, and keeps its program when the file stops compiling")
	{
		RecordingRenderer renderer;

		const ShaderFile file("reload.shader", "// ===COMMON===\nuniform float u_speed;\n" + STAGES);
		Loom::Shader shader(file.path.string());

		REQUIRE(shader.variables.size() == 1);

		const uint32_t first = shader.id;

		// Nothing written since it compiled.
		shader.ReloadIfChanged();
		CHECK(shader.id == first);

		file.Write("// ===COMMON===\nuniform float u_speed;\nuniform vec3 u_tint; // instance\n" + STAGES);

		const uint32_t firstInstanced = shader.instanced_id;
		const uint32_t firstSkinned = shader.skinned_id;

		CHECK(shader.Reload());
		CHECK(shader.id != first);
		CHECK(renderer.deleted == std::vector<uint32_t>{ first, firstInstanced, firstSkinned });
		REQUIRE(shader.variables.size() == 2);
		CHECK(shader.variables[1].scope == Loom::UniformScope::Instance);

		const uint32_t second = shader.id;

		file.Write("// ===COMMON===\nuniform float u_gone;\n// ===FRAGMENT===\nBROKEN\n");

		const LoomTests::Captured captured(std::cerr);
		const bool reloaded = shader.Reload();

		CHECK_FALSE(reloaded);
		CHECK(captured.Says("compile error"));
		CHECK(shader.id == second);
		CHECK(shader.variables.size() == 2);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "turning a shader's shadows on and off in its file builds and drops its shadow program")
	{
		RecordingRenderer renderer;

		const ShaderFile file("toggle shadows.shader", STAGES);
		Loom::Shader shader(file.path.string());

		CHECK(shader.shadow_id == 0);

		std::string error;
		REQUIRE(Loom::WriteShaderSettings(file.path.string(), { .shadows = true }, error));
		REQUIRE(shader.Reload());

		const uint32_t shadow = shader.shadow_id;

		CHECK(shader.settings.shadows);
		CHECK(shadow != 0);

		REQUIRE(Loom::WriteShaderSettings(file.path.string(), { }, error));
		REQUIRE(shader.Reload());

		CHECK_FALSE(shader.settings.shadows);
		CHECK(shader.shadow_id == 0);
		CHECK(std::find(renderer.deleted.begin(), renderer.deleted.end(), shadow) != renderer.deleted.end());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader path being typed keeps the material's shader until it names a shader file")
	{
		RecordingRenderer renderer;

		const ShaderFile file("typed path.shader", STAGES);

		Loom::Scene scene("typed path");
		Pump();

		Loom::Material* material = scene.Attach<Loom::Material>();
		Pump();

		material->ChangeShader(file.path.string());

		Loom::Shader* const named = material->shader;
		REQUIRE(named != nullptr);

		const Loom::SerializedField& path = material->GetFields().front();

		*(std::string*)path.data = file.path.parent_path().string();
		material->OnFieldChanged(path);
		CHECK(material->shader == named);

		*(std::string*)path.data = "";
		material->OnFieldChanged(path);
		CHECK(material->shader == nullptr);

		*(std::string*)path.data = file.path.string();
		material->OnFieldChanged(path);
		CHECK(material->shader == named);
	};
#endif

	#pragma endregion

	#pragma region Programs

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "each variable gets its own scope's value, or the default")
	{
		RecordingRenderer renderer;

		const std::vector<Loom::ShaderVariable> variables =
		{
			{ "u_speed", Loom::UniformType::Float, Loom::UniformScope::Material },
			{ "u_tint", Loom::UniformType::Vec3, Loom::UniformScope::Instance },
			{ "u_basis", Loom::UniformType::Mat2, Loom::UniformScope::Material },
			{ "u_count", Loom::UniformType::Int, Loom::UniformScope::Instance },
			{ "u_albedo", Loom::UniformType::Sampler2D, Loom::UniformScope::Material },
		};

		Loom::UniformValues material;
		material.Set("u_speed", Loom::UniformType::Float, { 2.0 });

		// Neither is the variable's: the tint is per instance, and the basis
		// is a mat2.
		material.Set("u_tint", Loom::UniformType::Vec3, { 9.0, 9.0, 9.0 });
		material.Set("u_basis", Loom::UniformType::Vec4, { 5.0, 5.0, 5.0, 5.0 });

		Loom::UniformValues instance;
		instance.Set("u_tint", Loom::UniformType::Vec3, { 0.5, 0.25, 1.0 });

		Loom::ApplyShaderVariables(1, variables, material, &instance);

		CHECK(renderer.uniforms["u_speed"].Float(0) == 2.0f);

		CHECK(renderer.uniforms["u_tint"].Float(0) == 0.5f);
		CHECK(renderer.uniforms["u_tint"].Float(1) == 0.25f);
		CHECK(renderer.uniforms["u_tint"].Float(2) == 1.0f);

		CHECK(renderer.uniforms["u_basis"].type == Loom::UniformType::Mat2);
		CHECK(renderer.uniforms["u_basis"].Float(0) == 1.0f);
		CHECK(renderer.uniforms["u_basis"].Float(1) == 0.0f);
		CHECK(renderer.uniforms["u_basis"].Float(3) == 1.0f);

		CHECK(renderer.uniforms["u_count"].Int(0) == 0);

		// No image named still puts something behind the sampler.
		CHECK(renderer.textures["u_albedo"] != 0);

		// Drawn with no instance values, a per-instance variable is put back to
		// its default rather than keeping the last object's.
		Loom::ApplyShaderVariables(1, variables, material, nullptr);

		CHECK(renderer.uniforms["u_tint"].Float(0) == 0.0f);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader with shadows on has its own shadow program, deleted along with it")
	{
		RecordingRenderer renderer;

		uint32_t main = 0;
		uint32_t shadow = 0;
		uint32_t instanced = 0;
		uint32_t shadowInstanced = 0;
		uint32_t skinned = 0;
		uint32_t shadowSkinned = 0;

		{
			std::istringstream source("// ===SETTINGS===\nshadows = on\n" + STAGES);
			Loom::Shader shader("own shadow program", source);

			main = shader.id;
			shadow = shader.shadow_id;
			instanced = shader.instanced_id;
			shadowInstanced = shader.shadow_instanced_id;
			skinned = shader.skinned_id;
			shadowSkinned = shader.shadow_skinned_id;

			CHECK(shader.settings.shadows);
			CHECK(shadow != 0);
			CHECK(shadow != main);
			CHECK(shadowInstanced != 0);
			CHECK(shadowSkinned != 0);
		};

		CHECK(renderer.deleted == std::vector<uint32_t>{ main, shadow, instanced, shadowInstanced, skinned, shadowSkinned });

		std::istringstream unshadowed(STAGES);
		CHECK(Loom::Shader("no shadow program", unshadowed).shadow_id == 0);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader has an instanced program reading its model matrix, colour and seed from each instance")
	{
		RecordingRenderer renderer;

		std::istringstream source(
			"// ===VERTEX===\nuniform highp mat4 u_model;\nuniform vec3 u_color;\nuniform float u_seed;\nvoid main() { }\n"
			"// ===FRAGMENT===\nuniform vec3 u_color;\nuniform float u_seed;\nvoid main() { }\n");
		Loom::Shader shader("instanced", source);

		REQUIRE(shader.instanced_id != 0);
		CHECK(shader.instanced_id != shader.id);

		const std::string& vertex = renderer.vertices.at("instanced instanced");
		CHECK(vertex.find("in mat4 loom_InstanceModel") != std::string::npos);
		CHECK(vertex.find("uniform highp mat4 u_model") == std::string::npos);
		CHECK(vertex.find("uniform float u_seed") == std::string::npos);
		CHECK(vertex.find("#define u_seed loom_InstanceColorSeed.a") != std::string::npos);
		CHECK(vertex.find("loom_instance_main()") != std::string::npos);

		const std::string& fragment = renderer.fragments.at("instanced instanced");
		CHECK(fragment.find("uniform vec3 u_color") == std::string::npos);
		CHECK(fragment.find("uniform float u_seed") == std::string::npos);
		CHECK(fragment.find("#define u_color loom_Color") != std::string::npos);
		CHECK(fragment.find("#define u_seed loom_Seed") != std::string::npos);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader that declares no model matrix, whose fragment stage reads it, or that declares its colour or seed otherwise, has no instanced program")
	{
		RecordingRenderer renderer;

		std::istringstream noModel(
			"// ===VERTEX===\nvoid main() { }\n"
			"// ===FRAGMENT===\nuniform vec3 u_color;\nvoid main() { }\n");
		CHECK(Loom::Shader("no model", noModel).instanced_id == 0);

		std::istringstream readsModel(
			"// ===VERTEX===\nuniform mat4 u_model;\nvoid main() { }\n"
			"// ===FRAGMENT===\nuniform mat4 u_model;\nvoid main() { }\n");
		CHECK(Loom::Shader("reads model", readsModel).instanced_id == 0);

		std::istringstream vec4Color(
			"// ===VERTEX===\nuniform mat4 u_model;\nvoid main() { }\n"
			"// ===FRAGMENT===\nuniform vec4 u_color;\nvoid main() { }\n");
		CHECK(Loom::Shader("vec4 colour", vec4Color).instanced_id == 0);

		std::istringstream vec2Seed(
			"// ===VERTEX===\nuniform mat4 u_model;\nuniform vec2 u_seed;\nvoid main() { }\n"
			"// ===FRAGMENT===\nvoid main() { }\n");
		CHECK(Loom::Shader("vec2 seed", vec2Seed).instanced_id == 0);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader has a skinned program that bends its vertex stage's position and normal by the joints")
	{
		RecordingRenderer renderer;

		std::istringstream source(
			"// ===VERTEX===\nlayout(location = 0) in vec3 aPos;\nlayout(location = 2) in highp vec3 aNormal;\nout vec3 n;\n"
			"void main() { n = aNormal; gl_Position = vec4(aPos, 1.0); }\n"
			"// ===FRAGMENT===\nvoid main() { }\n");
		Loom::Shader shader("skinned", source);

		REQUIRE(shader.skinned_id != 0);
		CHECK(shader.skinned_id != shader.id);

		const std::string& vertex = renderer.vertices.at("skinned skinned");
		CHECK(vertex.find("in vec3 aPos;") == std::string::npos);
		CHECK(vertex.find("in highp vec3 aNormal;") == std::string::npos);
		CHECK(vertex.find("layout(location = 0) in vec3 loom_BindPos;") != std::string::npos);
		CHECK(vertex.find("layout(location = 2) in vec3 loom_BindNormal;") != std::string::npos);
		CHECK(vertex.find("uniform mat4 u_joints[64];") != std::string::npos);
		CHECK(vertex.find("#define aPos loom_SkinnedPos") != std::string::npos);
		CHECK(vertex.find("loom_skin_main()") != std::string::npos);

		CHECK(renderer.fragments.at("skinned skinned") == renderer.fragments.at("skinned"));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a vertex stage that reads no aPos has no skinned program")
	{
		RecordingRenderer renderer;

		std::istringstream positionless("// ===VERTEX===\nvoid main() { gl_Position = vec4(0.0); }\n// ===FRAGMENT===\nvoid main() { }\n");
		Loom::Shader shader("positionless", positionless);

		CHECK(shader.skinned_id == 0);
		CHECK(shader.skinned_vertex.empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader whose UV layers reach the Skin's locations has no skinned program, and none may read the Skin's layer")
	{
		RecordingRenderer renderer;

		std::istringstream reaches("// ===VERTEX===\nUV_1<vec4 a, vec4 b, vec4 c, vec4 d>;\nUV_2<vec4 a, float b>;\nvoid main() { }\n// ===FRAGMENT===\nvoid main() { }\n");
		CHECK(Loom::Shader("reaches the skin", reaches).skinned_id == 0);

		std::istringstream short_of("// ===VERTEX===\nin vec3 aPos;\nUV_1<vec4 a, vec4 b, vec4 c, vec4 d>;\nUV_2<vec4>;\nvoid main() { }\n// ===FRAGMENT===\nvoid main() { }\n");
		CHECK(Loom::Shader("short of the skin", short_of).skinned_id != 0);

		std::istringstream skins("// ===VERTEX===\nUV_4294967295<float>;\nvoid main() { }\n// ===FRAGMENT===\nvoid main() { }\n");
		CHECK_THROWS_WITH_AS(Loom::Shader("reads the skin", skins), doctest::Contains("the Skin's"), std::runtime_error);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a second shader of the same name takes the compiled one's settings and shadow program")
	{
		RecordingRenderer renderer;

		std::istringstream source("// ===SETTINGS===\nshading = on\nshadows = on\n// ===FRAGMENT===\nout vec4 c;\nvoid main() { c = vec4(1.0); }\n");
		Loom::Shader first("cached settings", source);

		std::istringstream ignored("");
		Loom::Shader second("cached settings", ignored);

		CHECK(second.id == first.id);
		CHECK(second.shadow_id == first.shadow_id);
		CHECK(second.settings.shading);
		CHECK(second.settings.shadows);
	};

	#pragma endregion

	#pragma region Shadows and Drawing

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material with shadows off is left out of the shadow map and drawn fully lit")
	{
		RecordingRenderer renderer;

		std::istringstream source("// ===SETTINGS===\nshadows = on\n" + STAGES);
		Loom::Shader shader("shadow casters", source);

		std::istringstream unshadowed_source(STAGES);
		Loom::Shader unshadowed("shadow casters without shadows", unshadowed_source);

		Loom::Scene scene("shadow casters");
		Pump();

		Loom::Light* light = scene.Attach<Loom::Light>();

		std::vector<Loom::Material*> materials;

		for (const char* name : { "Shadowed", "Unshadowed", "Shader without shadows" })
		{
			Loom::GameObject* object = scene.AddChild(name);

			Loom::Material* material = object->Attach<Loom::Material>();
			material->shader = materials.size() < 2 ? &shader : &unshadowed;
			materials.push_back(material);

			object->Attach<Loom::Mesh>()->m_vertices = std::vector<float>{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
		};

		Pump();

		materials[1]->shadows = false;

		light->RenderShadowMap({ &scene.GetRoot() });

		// With the shader's own shadow program, seeing the scene from the light.
		CHECK(renderer.drawn == std::vector<uint32_t>{ shader.shadow_id });
		CHECK(renderer.matrices["u_viewProjection"] == light->ViewProjection());

		materials[0]->Apply(shader.id);
		CHECK(renderer.floats["u_shadowsOff"] == 0.0f);

		materials[1]->Apply(shader.id);
		CHECK(renderer.floats["u_shadowsOff"] == 1.0f);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the shadow pass draws a closed caster's far side and an open one's both, and culls nothing after")
	{
		RecordingRenderer renderer;

		std::istringstream source("// ===SETTINGS===\nshadows = on\n" + STAGES);
		Loom::Shader shader("far side casters", source);

		Loom::Scene scene("far side casters");
		Pump();

		Loom::Light* light = scene.Attach<Loom::Light>();

		const std::vector<float> triangle = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };

		for (const std::vector<float>* vertices : { &LoomTests::TETRAHEDRON, &triangle })
		{
			Loom::GameObject* object = scene.AddChild("Caster");
			object->Attach<Loom::Material>()->shader = &shader;
			object->Attach<Loom::Mesh>()->m_vertices = *vertices;
		};

		Pump();

		light->RenderShadowMap({ &scene.GetRoot() });

		CHECK(renderer.culls == std::vector<Loom::CullFace>{ Loom::CullFace::Front, Loom::CullFace::None, Loom::CullFace::None });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the shadow pass leaves out the faces an open caster's material hides, turned over by a mirroring transform, and keeps a closed caster's far side")
	{
		RecordingRenderer renderer;

		std::istringstream source("// ===SETTINGS===\nshadows = on\n" + STAGES);
		Loom::Shader shader("one-sided casters", source);

		Loom::Scene scene("one-sided casters");
		Pump();

		Loom::Light* light = scene.Attach<Loom::Light>();

		const std::vector<float> triangle = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };

		const auto add = [&scene, &shader](const std::vector<float>& vertices, Loom::Material::Sides sides)
		{
			Loom::GameObject* object = scene.AddChild("Caster");
			Loom::Material* material = object->Attach<Loom::Material>();
			material->shader = &shader;
			material->sides = sides;
			object->Attach<Loom::Mesh>()->m_vertices = vertices;

			return object;
		};

		add(triangle, Loom::Material::Front);
		add(triangle, Loom::Material::Back);
		add(triangle, Loom::Material::Front)->transform.scale = glm::vec3(-1.0f, 1.0f, 1.0f);
		add(LoomTests::TETRAHEDRON, Loom::Material::Front);

		Pump();

		light->RenderShadowMap({ &scene.GetRoot() });

		CHECK(renderer.culls == std::vector<Loom::CullFace>{ Loom::CullFace::Back, Loom::CullFace::Front, Loom::CullFace::Front, Loom::CullFace::Front, Loom::CullFace::None });
	};

	// The web's clock is the browser's, which cannot be stepped here.
#ifndef __EMSCRIPTEN__
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material hands its program the game time")
	{
		RecordingRenderer renderer;

		Loom::Scene scene("game time");
		Pump();

		Loom::Material* material = scene.AddChild("Timed")->Attach<Loom::Material>();
		Pump();

		// GLFW's time restarts with it, so two frames on it is neither still nor one frame's.
		constexpr std::chrono::milliseconds frame_length{ 20 };
		constexpr int frames = 2;
		glfwInit();
		Loom::Utilities::Tick();
		const float before = Loom::Time::SinceStart();

		for (int frame = 0; frame < frames; frame++)
		{
			std::this_thread::sleep_for(frame_length);
			Loom::Utilities::Tick();
		};

		REQUIRE(Loom::Time::SinceStart() - before > Loom::Time::DeltaTime());
		REQUIRE(Loom::Time::SinceStart() != 0.0f);

		material->Apply(1);
		CHECK(renderer.floats["u_time"] == Loom::Time::SinceStart());

		glfwTerminate();
	};
#endif

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "casters alike are drawn into the shadow map as instances of one")
	{
		RecordingRenderer renderer;

		std::istringstream source("// ===SETTINGS===\nshadows = on\n" + STAGES);
		Loom::Shader shader("instanced shadow casters", source);

		REQUIRE(shader.shadow_instanced_id != 0);

		Loom::Scene scene("instanced shadow casters");
		Pump();

		Loom::Light* light = scene.Attach<Loom::Light>();

		for (const char* name : { "First", "Second", "Third" })
		{
			Loom::GameObject* object = scene.AddChild(name);
			object->Attach<Loom::Material>()->shader = &shader;
			object->Attach<Loom::Mesh>()->m_vertices = std::vector<float>{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
		};

		Pump();

		light->RenderShadowMap({ &scene.GetRoot() });

		const uint32_t instanced = shader.shadow_instanced_id;

		CHECK(renderer.drawn == std::vector<uint32_t>{ instanced, instanced, instanced });
		CHECK(renderer.instanceCounts == std::vector<size_t>{ 3 });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a light's shadow map holds every scene drawn in the view that it falls on")
	{
		RecordingRenderer renderer;

		std::istringstream owner_source("// ===SETTINGS===\nshadows = on\n" + STAGES);
		Loom::Shader owner_shader("shadows in the light's scene", owner_source);

		std::istringstream borrower_source("// ===SETTINGS===\nshadows = on\n" + STAGES);
		Loom::Shader borrower_shader("shadows in the borrowing scene", borrower_source);

		Loom::Scene owner("owns the light");
		Loom::Scene borrower("borrows the light");
		Pump();

		owner.Attach<Loom::Light>();

		for (const auto& [scene, shader] : { std::pair{ &owner, &owner_shader }, std::pair{ &borrower, &borrower_shader } })
		{
			Loom::GameObject* object = scene->AddChild("Caster");
			object->Attach<Loom::Material>()->shader = shader;
			object->Attach<Loom::Mesh>()->m_vertices = std::vector<float>{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
		};

		Pump();

		// The shadow pass, both scenes in it, then the scene itself.
		owner.Render();
		CHECK(renderer.drawn == std::vector<uint32_t>{ owner_shader.shadow_id, borrower_shader.shadow_id, owner_shader.id });

		renderer.drawn.clear();
		borrower.Render();
		CHECK(renderer.drawn == std::vector<uint32_t>{ borrower_shader.shadow_id, owner_shader.shadow_id, borrower_shader.id });

		// A view drawing one scene alone shows no shadow from the other.
		renderer.drawn.clear();
		owner.Render(nullptr, { &owner });
		CHECK(renderer.drawn == std::vector<uint32_t>{ owner_shader.shadow_id, owner_shader.id });

		renderer.drawn.clear();
		borrower.Render(nullptr, { &borrower });
		CHECK(renderer.drawn == std::vector<uint32_t>{ borrower_shader.shadow_id, borrower_shader.id });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader with transparency on is drawn after everything opaque, farthest first, depth before colour")
	{
		RecordingRenderer renderer;

		std::istringstream opaque_source(STAGES);
		Loom::Shader opaque("opaque", opaque_source);

		std::istringstream near_source("// ===SETTINGS===\ntransparency = on\n" + STAGES);
		Loom::Shader near("transparent near", near_source);

		std::istringstream far_source("// ===SETTINGS===\ntransparency = on\n" + STAGES);
		Loom::Shader far("transparent far", far_source);

		CHECK(near.settings.transparency);

		Loom::Scene scene("transparency");
		Pump();

		// In hierarchy order the near one comes first, and the opaque one last.
		// Without a camera, world space is clip space, where a greater z is farther.
		constexpr float NEAR_Z = -0.5f;
		constexpr float FAR_Z = 0.5f;

		const std::tuple<const char*, Loom::Shader*, float> placed[] = { { "Near", &near, NEAR_Z }, { "Far", &far, FAR_Z }, { "Opaque", &opaque, 0.0f } };

		for (const auto& [name, shader, z] : placed)
		{
			Loom::GameObject* object = scene.AddChild(name);
			object->transform.position = glm::vec3(0.0f, 0.0f, z);
			object->Attach<Loom::Material>()->shader = shader;
			object->Attach<Loom::Mesh>()->m_vertices = std::vector<float>{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
		};

		Pump();

		scene.Render();

		CHECK(renderer.drawn == std::vector<uint32_t>{ opaque.id, far.id, far.id, near.id, near.id });
		CHECK(renderer.colorWrites == std::vector<bool>{ false, true, false, true });
		CHECK(renderer.depthTestsEqual == std::vector<bool>{ true, false });
	};

	#pragma endregion
};
