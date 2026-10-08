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

#include <array>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using LoomTests::Pump;


namespace
{
	// Stands in for the Engine's renderer while it lives, which a test has no
	// window to open. It hands out handles, records what it is given, and
	// compiles anything but a fragment stage that says BROKEN.
	struct RecordingRenderer final : Loom::Renderer
	{
		static constexpr const char* BROKEN = "BROKEN";
		static constexpr int MAX_TEXTURE_SIZE = 4096;

		struct Uniform
		{
			Loom::UniformType type;
			std::array<uint32_t, Loom::MAX_UNIFORM_COMPONENTS> components{ };

			float Float(int i) const { return std::bit_cast<float>(components[i]); };
			int32_t Int(int i) const { return (int32_t)components[i]; };
		};

		RecordingRenderer() : m_previous(current) { current = this; };
		~RecordingRenderer() { current = m_previous; };

		Loom::Backend GetBackend() const override { return Loom::Backend::OpenGL; };
		bool Attach(GLFWwindow*) override { return true; };

		void InitImGui(GLFWwindow*) override { };
		void ShutdownImGui() override { };
		void NewImGuiFrame() override { };
		void RenderImGui(ImDrawData*) override { };
		void RenderImGuiWindows() override { };
		void ReleaseImGuiFonts() override { };
		void BeginFrame() override { };
		void EndFrame() override { };
		void Capture(const CaptureHandler&) override { };

		uint32_t CreateProgram(const std::string& name, const std::string&, const std::string& fragment) override
		{
			if (fragment.find(BROKEN) != std::string::npos)
				throw std::runtime_error("Shader compile error in " + name);

			return ++m_handles;
		};

		void DeleteProgram(uint32_t program) override { deleted.push_back(program); };

		void SetUniform(uint32_t, const char* name, float value) override { floats[name] = value; };
		void SetUniform(uint32_t, const char*, const glm::vec3&) override { };
		void SetUniform(uint32_t, const char*, const glm::vec4&) override { };
		void SetUniform(uint32_t, const char*, const glm::mat4&) override { };

		void SetUniform(uint32_t, const char* name, Loom::UniformType type, const void* components) override
		{
			const Loom::UniformTypeInfo& info = Loom::InfoOf(type);

			Uniform& uniform = uniforms[name];
			uniform.type = type;
			std::copy_n((const uint32_t*)components, info.columns * info.rows, uniform.components.begin());
		};

		void SetTexture(uint32_t, const char* name, uint32_t texture) override { textures[name] = texture; };

		void Draw(uint32_t, uint32_t, const float*, size_t, uint32_t) override { draws++; };

		uint32_t CreateTexture(int, int, const uint8_t*) override { return ++m_handles; };
		uint32_t CreateDepthTarget(int) override { return ++m_handles; };
		uint32_t CreateColorTarget(int, int) override { return ++m_handles; };
		void DestroyTexture(uint32_t) override { };

		void PushTarget(uint32_t) override { };
		void PopTarget() override { };
		void Clear(const float*, bool) override { };
		void SetDepthBias(float, float) override { };

		glm::ivec2 TargetSize() const override { return glm::ivec2(1); };
		int MaxTextureSize() const override { return MAX_TEXTURE_SIZE; };
		void* ImGuiTexture(uint32_t) override { return nullptr; };

		std::map<std::string, float> floats;
		std::map<std::string, Uniform> uniforms;
		std::map<std::string, uint32_t> textures;
		std::vector<uint32_t> deleted;
		size_t draws = 0;

	private:
		Renderer* m_previous;
		uint32_t m_handles = 0;
	};

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

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material with shadows off is left out of the shadow map and drawn fully lit")
	{
		RecordingRenderer renderer;

		std::istringstream source(STAGES);
		Loom::Shader shader("shadow casters", source);

		Loom::Scene scene("shadow casters");
		Pump();

		Loom::Light* light = scene.Attach<Loom::Light>();

		std::vector<Loom::Material*> materials;

		for (const char* name : { "Shadowed", "Unshadowed" })
		{
			Loom::GameObject* object = scene.AddChild(name);

			Loom::Material* material = object->Attach<Loom::Material>();
			material->shader = &shader;
			materials.push_back(material);

			object->Attach<Loom::Mesh>()->m_vertices = std::vector<float>{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
		};

		Pump();

		materials[1]->shadows = false;

		light->RenderShadowMap(scene.GetRoot());

		CHECK(renderer.draws == 1);

		materials[0]->Apply(shader.id);
		CHECK(renderer.floats["u_shadowsOff"] == 0.0f);

		materials[1]->Apply(shader.id);
		CHECK(renderer.floats["u_shadowsOff"] == 1.0f);
	};
};
