#include "doctest.h"

#include "Test Support.h"

#include "Material.h"
#include "PreVertex.h"
#include "Scene.h"
#include "SerializedField.h"
#include "Shaders.h"

#ifndef __EMSCRIPTEN__
#include "GlslCompiler.h"
#include "ProjectAssets.h"
#include "RenderingObjects.h"
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using LoomTests::Pump;


#pragma region Helpers

namespace
{
	const std::string VERTEX =
		"// ===VERTEX===\n"
		"layout(location = 0) in vec3 aPos;\n"
		"uniform mat4 u_viewProjection;\n"
		"uniform mat4 u_model;\n"
		"uniform float u_time;\n"
		"void main() { gl_Position = u_viewProjection * u_model * vec4(aPos * (1.0 + u_time), 1.0); }\n";

	const std::string FRAGMENT =
		"// ===FRAGMENT===\n"
		"out vec4 FragColor;\n"
		"void main() { FragColor = vec4(1.0); }\n";

	const std::string WAVE =
		"uniform float u_time;\n"
		"vec3 PreVertex(vec3 world)\n"
		"{\n"
		"\tworld.y += sin(world.x + u_time);\n"
		"\treturn world;\n"
		"}\n";

	// A file under the temp folder holding text, gone when this is.
	struct TempFile final
	{
		std::filesystem::path path;

		TempFile(const std::string& name, const std::string& text) :
			path(std::filesystem::temp_directory_path() / name)
		{
			std::ofstream(path) << text;
		};

		~TempFile()
		{
			std::error_code code;
			std::filesystem::remove(path, code);
		};

		std::string Path() const { return path.generic_string(); };
	};

#ifndef __EMSCRIPTEN__
	bool Compiles(const std::string& vertex, const std::string& fragment)
	{
		const Loom::Glsl::Program program = Loom::Glsl::Compile(vertex, fragment);

		INFO(program.log);

		return program.compiled;
	};
#endif

	const Loom::SerializedField& FieldOf(const Loom::LoomObject& object, const void* data)
	{
		const auto& fields = object.GetFields();

		return *std::find_if(fields.begin(), fields.end(), [&](const Loom::SerializedField& field) { return field.data == data; });
	};
};

#pragma endregion


TEST_SUITE("PreVertex")
{
	#pragma region Assembly

	TEST_CASE("assembling with pre-vertex code moves aPos through it in world space before the stage's own main")
	{
		const Loom::Shader::Stages stages = Loom::Shader::Assemble(VERTEX + FRAGMENT, WAVE);

		CHECK(stages.vertex.find("void loom_pre_vertex_main()") != std::string::npos);
		CHECK(stages.vertex.find("loom_PreVertexPos = (inverse(u_model) * vec4(PreVertex(") != std::string::npos);
		CHECK(stages.vertex.find("vec4(loom_PreVertexPos * (1.0 + u_time), 1.0)") != std::string::npos);
		CHECK(stages.vertex.find("in vec3 aPos;") != std::string::npos);

		// The stage declares u_time already, so the code's own is left out.
		CHECK(stages.vertex.find("uniform float u_time;") == stages.vertex.rfind("uniform float u_time;"));
	};

	TEST_CASE("a model flattened to no scale on an axis keeps its vertices rather than an inverse it has none of")
	{
		const Loom::Shader::Stages stages = Loom::Shader::Assemble(VERTEX + FRAGMENT, WAVE);
		const size_t guard = stages.vertex.find("if (determinant(u_model) != 0.0)");

		REQUIRE(guard != std::string::npos);
		CHECK(guard < stages.vertex.find("inverse(u_model)"));
		CHECK(stages.vertex.find("loom_PreVertexPos = aPos;") < guard);
	};

	TEST_CASE("assembling without pre-vertex code leaves the vertex stage as it was")
	{
		CHECK(Loom::Shader::Assemble(VERTEX + FRAGMENT, "").vertex == Loom::Shader::Assemble(VERTEX + FRAGMENT).vertex);
	};

	TEST_CASE("a vertex stage with no u_model is left as it was")
	{
		const std::string vertex =
			"// ===VERTEX===\n"
			"layout(location = 0) in vec3 aPos;\n"
			"void main() { gl_Position = vec4(aPos, 1.0); }\n";

		CHECK(Loom::Shader::Assemble(vertex + FRAGMENT, WAVE).vertex == Loom::Shader::Assemble(vertex + FRAGMENT).vertex);
	};

	TEST_CASE("a shaded shader's shadow pass runs the pre-vertex code too")
	{
		const Loom::Shader::Stages stages = Loom::Shader::Assemble("// ===SETTINGS===\nshading = on\nshadows = on\n" + FRAGMENT, WAVE);

		CHECK(stages.vertex.find("loom_pre_vertex_main") != std::string::npos);
		CHECK(stages.shadowVertex.find("loom_pre_vertex_main") != std::string::npos);
	};

#ifndef __EMSCRIPTEN__
	TEST_CASE("plain and shaded stages with pre-vertex code compile, shadow pass and all")
	{
		const Loom::Shader::Stages plain = Loom::Shader::Assemble(VERTEX + FRAGMENT, WAVE);

		CHECK(Compiles(plain.vertex, plain.fragment));

		const Loom::Shader::Stages shaded = Loom::Shader::Assemble("// ===SETTINGS===\nshading = on\nshadows = on\n" + FRAGMENT, WAVE);

		CHECK(Compiles(shaded.vertex, shaded.fragment));
		CHECK(Compiles(shaded.shadowVertex, shaded.shadowFragment));
	};

	TEST_CASE("CheckPreVertex has nothing to say about code that compiles")
	{
		CHECK(Loom::Shader::CheckPreVertex(WAVE).empty());
	};

	TEST_CASE("CheckPreVertex puts a mistake on the line of the code it is on")
	{
		const std::string code =
			"vec3 PreVertex(vec3 world)\n"
			"{\n"
			"\treturn world * undeclaredScale;\n"
			"}\n";

		const std::vector<Loom::ShaderMessage> messages = Loom::Shader::CheckPreVertex(code);

		REQUIRE_FALSE(messages.empty());
		CHECK(messages[0].error);
		CHECK(messages[0].text.find("undeclaredScale") != std::string::npos);

		for (const Loom::ShaderMessage& message : messages)
			CHECK(message.line == 3);
	};

	TEST_CASE("CheckPreVertex reports code with no PreVertex")
	{
		CHECK_FALSE(Loom::Shader::CheckPreVertex("float Unused() { return 1.0; }\n").empty());
	};

	TEST_CASE("a new pre-vertex file holds code that compiles")
	{
		const std::filesystem::path folder = std::filesystem::temp_directory_path() / "loom new pre vertex";
		std::filesystem::remove_all(folder);
		std::filesystem::create_directories(folder);

		const std::string path = Loom::ProjectAssets::CreatePreVertex(folder.string(), "Wave");

		std::ifstream file(path);
		const std::string text((std::istreambuf_iterator<char>(file)), { });
		file.close();

		std::filesystem::remove_all(folder);

		CHECK(path.ends_with(Loom::PreVertex::extension));
		CHECK(text == Loom::ProjectAssets::defaultPreVertex);
		CHECK(Loom::Shader::CheckPreVertex(text).empty());
	};
#endif

	#pragma endregion

#ifndef __EMSCRIPTEN__
	#pragma region Component

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "with everything on, every shader runs the code, and none once it is detached")
	{
		const TempFile file("loom pre vertex everything.loomprevertex", WAVE);

		Loom::Scene scene("pre vertex everything");
		Loom::PreVertex* preVertex = scene.Attach<Loom::PreVertex>();
		preVertex->file = file.Path();
		Pump();

		CHECK(Loom::PreVertex::CodeFor("Assets/Lit.shader") == WAVE);
		CHECK(Loom::PreVertex::CodeFor("Assets/Other") == WAVE);

		scene.GetRoot().DetachComponent(preVertex);
		Pump();

		CHECK(Loom::PreVertex::CodeFor("Assets/Lit.shader").empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader the engine carries rather than reads off disk runs no code, even with everything on")
	{
		LoomTests::RecordingRenderer renderer;

		const TempFile file("loom pre vertex carried.loomprevertex", WAVE);

		Loom::Scene scene("pre vertex carried");
		scene.Attach<Loom::PreVertex>()->file = file.Path();
		Pump();

		REQUIRE(Loom::PreVertex::CodeFor("loom pre vertex carried") == WAVE);

		std::istringstream source(VERTEX + FRAGMENT);
		const Loom::Shader shader("loom pre vertex carried", source);

		REQUIRE(renderer.vertices.contains("loom pre vertex carried"));
		CHECK(renderer.vertices.at("loom pre vertex carried").find("loom_pre_vertex_main") == std::string::npos);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "with everything off, only the shaders listed run the code, however their paths are spelt")
	{
		const TempFile file("loom pre vertex listed.loomprevertex", WAVE);

		Loom::Scene scene("pre vertex listed");
		Loom::PreVertex* preVertex = scene.Attach<Loom::PreVertex>();
		preVertex->file = file.Path();
		preVertex->everything = false;
		preVertex->shaders = { "Assets/./Lit.shader", "Assets/Water" };
		Pump();

		CHECK(Loom::PreVertex::CodeFor("Assets/Lit.shader") == WAVE);
		CHECK(Loom::PreVertex::CodeFor("Assets/Lit") == WAVE);
		CHECK(Loom::PreVertex::CodeFor("Assets/Water.shader") == WAVE);
		CHECK(Loom::PreVertex::CodeFor("Assets/Rock.shader").empty());

		scene.GetRoot().DetachComponent(preVertex);
		Pump();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the last one attached is the one in effect, and the one before takes over once it goes")
	{
		const TempFile first("loom pre vertex first.loomprevertex", WAVE);
		const TempFile second("loom pre vertex second.loomprevertex", "vec3 PreVertex(vec3 world) { return world; }\n");

		Loom::Scene scene("pre vertex last");

		Loom::PreVertex* older = scene.Attach<Loom::PreVertex>();
		older->file = first.Path();

		Loom::PreVertex* newer = scene.GetRoot().AddChild("Newer")->Attach<Loom::PreVertex>();
		newer->file = second.Path();
		Pump();

		CHECK(Loom::PreVertex::CodeFor("Assets/Lit.shader") == "vec3 PreVertex(vec3 world) { return world; }\n");

		newer->GetGameObject()->DetachComponent(newer);
		Pump();

		CHECK(Loom::PreVertex::CodeFor("Assets/Lit.shader") == WAVE);

		scene.GetRoot().DetachComponent(older);
		Pump();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "one in a scene that unloads stops running its code")
	{
		const TempFile file("loom pre vertex unloaded.loomprevertex", WAVE);

		{
			Loom::Scene scene("pre vertex unloaded");
			scene.Attach<Loom::PreVertex>()->file = file.Path();
			Pump();

			CHECK(Loom::PreVertex::CodeFor("Assets/Lit.shader") == WAVE);
		};

		Pump();

		CHECK(Loom::PreVertex::CodeFor("Assets/Lit.shader").empty());
		Loom::Material::ReloadChangedShaders();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the Rendering menu's Pre-Vertex is a child running the file, in effect once attached")
	{
		const TempFile file("loom pre vertex menu.loomprevertex", WAVE);

		Loom::Scene scene("pre vertex menu");
		Pump();

		Loom::GameObject* made = Loom::RenderingObjects::AddPreVertex(scene.GetRoot(), file.Path());
		Pump();

		REQUIRE(made != nullptr);
		CHECK(made->GetName() == Loom::RenderingObjects::PreVertexLabel);
		CHECK(made->GetParent() == &scene.GetRoot());

		const Loom::PreVertex* preVertex = made->GetComponent<Loom::PreVertex>();

		REQUIRE(preVertex != nullptr);
		CHECK((const std::string&)*preVertex->file == file.Path());
		CHECK(Loom::PreVertex::CodeFor("Assets/Lit.shader") == WAVE);

		scene.GetRoot().RemoveChild(made);
		Pump();

		CHECK(Loom::PreVertex::CodeFor("Assets/Lit.shader").empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the list of shaders is shown only while everything is off")
	{
		Loom::PreVertex preVertex;

		const Loom::SerializedField& shaders = FieldOf(preVertex, &*preVertex.shaders);

		CHECK(shaders.type == Loom::FieldType::AssetList);
		CHECK(shaders.extensions == std::vector<std::string>{ Loom::Shader::extension });
		CHECK(FieldOf(preVertex, &*preVertex.file).extensions == std::vector<std::string>{ Loom::PreVertex::extension });

		CHECK_FALSE(preVertex.IsShown(shaders));
		CHECK(preVertex.IsShown(FieldOf(preVertex, &*preVertex.file)));

		preVertex.everything = false;

		CHECK(preVertex.IsShown(shaders));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material's shader compiles again with the code once a PreVertex covers it, every program it makes compiling")
	{
		LoomTests::RecordingRenderer renderer;

		const TempFile shader("loom pre vertex covered.shader", VERTEX + FRAGMENT);
		const TempFile file("loom pre vertex covered.loomprevertex", WAVE);

		Loom::Scene scene("pre vertex covered");
		scene.Attach<Loom::Material>()->SetShaderPath(shader.Path());
		Pump();

		const auto covered = [&]()
		{
			size_t count = 0;

			for (const auto& [name, vertex] : renderer.vertices)
				if (name.starts_with(shader.Path()) && vertex.find("loom_pre_vertex_main") != std::string::npos)
				{
					CHECK_MESSAGE(Compiles(vertex, renderer.fragments.at(name)), name);
					count++;
				};

			return count;
		};

		REQUIRE(renderer.vertices.contains(shader.Path()));
		CHECK(covered() == 0);

		Loom::PreVertex* preVertex = scene.GetRoot().AddChild("Wave")->Attach<Loom::PreVertex>();
		preVertex->file = file.Path();
		Pump();

		// The program, and its instanced and skinned twins.
		CHECK(covered() >= 3);

		preVertex->everything = false;
		preVertex->OnFieldChanged(FieldOf(*preVertex, &*preVertex->everything));

		CHECK(renderer.vertices.at(shader.Path()).find("loom_pre_vertex_main") == std::string::npos);

		preVertex->GetGameObject()->DetachComponent(preVertex);
		Pump();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "writing the file compiles the shaders it covers again with the new code")
	{
		LoomTests::RecordingRenderer renderer;

		const TempFile shader("loom pre vertex rewritten.shader", VERTEX + FRAGMENT);
		const TempFile file("loom pre vertex rewritten.loomprevertex", WAVE);

		Loom::Scene scene("pre vertex rewritten");
		Loom::PreVertex* preVertex = scene.Attach<Loom::PreVertex>();
		preVertex->file = file.Path();
		Loom::PreVertex::ReloadIfChanged();

		scene.GetRoot().AddChild("Mesh")->Attach<Loom::Material>()->SetShaderPath(shader.Path());
		Pump();

		REQUIRE(renderer.vertices.contains(shader.Path()));
		CHECK(renderer.vertices.at(shader.Path()).find("sin(world.x + u_time)") != std::string::npos);

		std::ofstream(file.path) << "vec3 PreVertex(vec3 world) { return world * 2.0; }\n";
		std::filesystem::last_write_time(file.path, std::filesystem::last_write_time(file.path) + std::chrono::seconds(1));

		Loom::Material::ReloadChangedShaders();

		CHECK(renderer.vertices.at(shader.Path()).find("return world * 2.0;") != std::string::npos);

		scene.GetRoot().DetachComponent(preVertex);
		Pump();
	};

	#pragma endregion
#endif
};
