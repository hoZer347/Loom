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

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using LoomTests::Pump;
using LoomTests::RecordingRenderer;


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
		"uniform mat4 u_model;\n"
		"void main() { }\n"
		"// ===FRAGMENT===\n"
		"void main() { }\n";
};


TEST_SUITE("Shader variables")
{
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

	TEST_CASE("declaring into a file without a COMMON section adds one in front of the stages")
	{
		const ShaderFile file("declare without common.shader", "// A header comment.\n\n" + STAGES);

		std::string error;
		REQUIRE(Loom::DeclareShaderVariable(file.path.string(), "", { "u_tint", Loom::UniformType::Vec3, Loom::UniformScope::Instance }, error));

		const std::string text = file.Text();

		CHECK(text.find("// A header comment.\n\n// ===COMMON===\n\nuniform vec3 u_tint; // instance\n\n// ===VERTEX===") != std::string::npos);

		const std::vector<Loom::ShaderVariable> variables = Loom::ParseShaderVariables(text);

		REQUIRE(variables.size() == 1);
		CHECK(variables[0].scope == Loom::UniformScope::Instance);
	};

	TEST_CASE("a new declaration goes after the ones already in COMMON, keeping the file's line endings")
	{
		const ShaderFile file(
			"declare after.shader",
			"// ===COMMON===\r\n"
			"precision highp sampler2DShadow;\r\n"
			"uniform float u_first; // material\r\n"
			"\r\n"
			"float Helper() { return 1.0; }\r\n"
			"// ===VERTEX===\r\n");

		std::string error;
		REQUIRE(Loom::DeclareShaderVariable(file.path.string(), "", { "u_second", Loom::UniformType::Int }, error));

		CHECK(file.Text() ==
			"// ===COMMON===\r\n"
			"precision highp sampler2DShadow;\r\n"
			"uniform float u_first; // material\r\n"
			"uniform int u_second; // material\r\n"
			"\r\n"
			"float Helper() { return 1.0; }\r\n"
			"// ===VERTEX===\r\n");
	};

	TEST_CASE("writing the settings into a file without them adds the section in front of the stages")
	{
		const ShaderFile file("settings without section.shader", "// A header comment.\n\n" + STAGES);

		std::string error;
		REQUIRE(Loom::WriteShaderSettings(file.path.string(), { .shading = true }, error));

		CHECK(file.Text() == "// A header comment.\n\n// ===SETTINGS===\nshading = on\nshadows = off\ntransparency = off\n\n" + STAGES);

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
			"\r\n"
			"// ===FRAGMENT===\r\n"
			"void main() { }\r\n");

		// A variable declared afterwards goes in its own section, not the settings.
		REQUIRE(Loom::DeclareShaderVariable(file.path.string(), "", { "u_speed" }, error));
		CHECK_NOTHROW(Loom::ParseShaderSettings(file.Text()));
		CHECK(Loom::ParseShaderVariables(file.Text()).size() == 1);
	};

	TEST_CASE("redeclaring a variable rewrites its line where it is")
	{
		const ShaderFile file("redeclare.shader", "// ===FRAGMENT===\n\tuniform float u_speed;\nvoid main() { }\n");

		std::string error;
		REQUIRE(Loom::DeclareShaderVariable(file.path.string(), "u_speed", { "u_speed", Loom::UniformType::Vec2, Loom::UniformScope::Instance }, error));

		CHECK(file.Text() == "// ===FRAGMENT===\n\tuniform vec2 u_speed; // instance\nvoid main() { }\n");
	};

	TEST_CASE("a name that is taken, the engine's or not GLSL is refused")
	{
		const ShaderFile file("refused.shader", "// ===COMMON===\nuniform float u_speed;\n" + STAGES);
		const std::string before = file.Text();

		std::string error;

		CHECK_FALSE(Loom::DeclareShaderVariable(file.path.string(), "", { "u_speed" }, error));
		CHECK_FALSE(error.empty());

		CHECK_FALSE(Loom::DeclareShaderVariable(file.path.string(), "", { "u_model" }, error));
		CHECK_FALSE(Loom::DeclareShaderVariable(file.path.string(), "", { "2fast" }, error));
		CHECK_FALSE(Loom::DeclareShaderVariable(file.path.string(), "", { "gl_Mine" }, error));

		CHECK(file.Text() == before);
	};

	TEST_CASE("removing a variable takes its line out of the file")
	{
		const ShaderFile file("remove.shader", "// ===COMMON===\nuniform float u_speed; // material\nuniform vec3 u_tint;\n" + STAGES);

		std::string error;
		REQUIRE(Loom::RemoveShaderVariable(file.path.string(), "u_speed", error));

		CHECK(file.Text() == "// ===COMMON===\nuniform vec3 u_tint;\n" + STAGES);

		CHECK_FALSE(Loom::RemoveShaderVariable(file.path.string(), "u_speed", error));
	};

	// One variable to the table, so every line declaring it goes with it:
	// stages that disagree on a type do not link.
	TEST_CASE("a uniform both stages declare is retyped and removed in both")
	{
		const ShaderFile file(
			"both stages.shader",
			"// ===VERTEX===\nuniform float u_wave;\nvoid main() { }\n"
			"// ===FRAGMENT===\nuniform float u_wave;\nvoid main() { }\n");

		std::string error;
		REQUIRE(Loom::DeclareShaderVariable(file.path.string(), "u_wave", { "u_wave", Loom::UniformType::Vec2 }, error));

		CHECK(file.Text() ==
			"// ===VERTEX===\nuniform vec2 u_wave; // material\nvoid main() { }\n"
			"// ===FRAGMENT===\nuniform vec2 u_wave; // material\nvoid main() { }\n");

		REQUIRE(Loom::RemoveShaderVariable(file.path.string(), "u_wave", error));

		CHECK(file.Text() == "// ===VERTEX===\nvoid main() { }\n// ===FRAGMENT===\nvoid main() { }\n");
		CHECK(Loom::ParseShaderVariables(file.Text()).empty());
	};

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

		CHECK(shader.Reload());
		CHECK(shader.id != first);
		CHECK(renderer.deleted == std::vector<uint32_t>{ first });
		REQUIRE(shader.variables.size() == 2);
		CHECK(shader.variables[1].scope == Loom::UniformScope::Instance);

		const uint32_t second = shader.id;

		file.Write("// ===COMMON===\nuniform float u_gone;\n// ===FRAGMENT===\nBROKEN\n");

		std::ostringstream captured;
		std::streambuf* previous = std::cerr.rdbuf(captured.rdbuf());
		const bool reloaded = shader.Reload();
		std::cerr.rdbuf(previous);

		CHECK_FALSE(reloaded);
		CHECK(captured.str().find("compile error") != std::string::npos);
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

		{
			std::istringstream source("// ===SETTINGS===\nshadows = on\n" + STAGES);
			Loom::Shader shader("own shadow program", source);

			main = shader.id;
			shadow = shader.shadow_id;

			CHECK(shader.settings.shadows);
			CHECK(shadow != 0);
			CHECK(shadow != main);
		};

		CHECK(renderer.deleted == std::vector<uint32_t>{ main, shadow });

		std::istringstream unshadowed(STAGES);
		CHECK(Loom::Shader("no shadow program", unshadowed).shadow_id == 0);
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
};
