#include "doctest.h"

#include "ShaderVariables.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>


namespace
{
	// A shader file in the temp folder, gone again when the test is.
	struct ShaderFile
	{
		std::filesystem::path path;

		ShaderFile(const std::string& name, const std::string& source) :
			path(std::filesystem::temp_directory_path() / name)
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
};
