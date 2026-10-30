#include "doctest.h"

#include "Test Support.h"

#include "Material.h"
#include "PreVertex.h"
#include "Scene.h"
#include "SerializedField.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>

using LoomTests::Pump;

#pragma region Helpers

namespace
{
	// Path, colour, shadows, variables, sampling, sides.
	constexpr size_t FIELDS = 6;

	enum { PATH, COLOR, SHADOWS, VARIABLES, SAMPLING, SIDES };

	const std::string VERTEX =
		"// ===VERTEX===\n"
		"layout(location = 0) in vec3 aPos;\n"
		"uniform mat4 u_viewProjection;\n"
		"uniform mat4 u_model;\n"
		"void main() { gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0); }\n";

	std::string Fragment(const std::string& colour)
	{
		return "// ===FRAGMENT===\nout vec4 FragColor;\nvoid main() { FragColor = " + colour + "; }\n";
	};

	struct Paint final : Loom::Component<Paint>
	{
		Loom::Serial<float> wear = 0.5f;
		Loom::Serial<glm::vec3, "Colours/Tint"> tint = glm::vec3(1.0f, 0.5f, 0.25f);
		Loom::Serial<std::string> image = "Assets/grain.png";
	};
};

#pragma endregion


TEST_SUITE("Material")
{
	#pragma region Components

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a fresh material has no shader")
	{
		Loom::Material material;
		Pump();

		CHECK(material.shader == nullptr);
	};

	TEST_CASE("a material is a component and registers itself for rendering")
	{
		CHECK(std::is_base_of_v<Loom::Component<Loom::Material, "Rendering">, Loom::Material>);
		CHECK(std::is_base_of_v<Loom::ComponentBase, Loom::Material>);
	};

	// Mesh issues the draw, so a material renders safely with no GL context.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "rendering a material on its own draws nothing")
	{
		Loom::Scene scene("material render");
		Pump();

		Loom::Material* material = scene.Attach<Loom::Material>();
		Pump();

		scene.Render();

		CHECK(material->shader == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh finds its material on the same GameObject")
	{
		Loom::Scene scene("material lookup");
		Pump();

		Loom::Material* material = scene.Attach<Loom::Material>();
		Pump();

		CHECK(scene.GetRoot().GetComponent<Loom::Material>() == material);
	};

	// The engine compiles one shader per path and shares it.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material carries the name of the shader it wants")
	{
		Loom::Scene scene("shader path");
		Pump();

		Loom::Material* material = scene.Attach<Loom::Material>();
		Pump();

		CHECK(material->GetShaderPath().empty());

		material->SetShaderPath("Assets/Shader.shader");

		CHECK(material->GetShaderPath() == "Assets/Shader.shader");

		// It is a serialized field, so the scene file carries it too.
		REQUIRE(material->GetFields().size() == FIELDS);
		CHECK(material->GetFields().front().Write() == "\"Assets/Shader.shader\"");

		// Picked in the editor from the project's shader files.
		CHECK(material->GetFields().front().type == Loom::FieldType::Asset);
		CHECK(material->GetFields().front().extensions == std::vector<std::string>{ Loom::Shader::extension });
	};

	#pragma endregion

	#pragma region Fields

	// The path comes first: it was the only field before colour was added, and
	// scene files already saved key it by position.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material is white until given a colour, and saves it after the path")
	{
		Loom::Material material;
		Pump();

		const glm::vec3& color = material.color;
		CHECK(color.x == 1.0f);
		CHECK(color.y == 1.0f);
		CHECK(color.z == 1.0f);

		material.color = glm::vec3(0.25f, 0.5f, 0.75f);

		REQUIRE(material.GetFields().size() == FIELDS);
		CHECK(material.GetFields()[COLOR].Write() == "(0.25, 0.5, 0.75)");
	};

	// After colour, for the same reason colour is after the path.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material is shadowed until told otherwise, and saves its variables last")
	{
		Loom::Material material;
		Pump();

		CHECK(material.shadows);
		CHECK(material.GetFields()[SHADOWS].Write() == "true");

		material.variables->Set("u_speed", Loom::UniformType::Float, { 2.0 });
		material.variables->Set("u_tint", Loom::UniformType::Vec3, { 1.0, 0.5, 0.25 });

		const Loom::SerializedField& field = material.GetFields()[VARIABLES];

		CHECK(field.type == Loom::FieldType::Uniforms);
		CHECK(field.Write() == "\"u_speed float 2\\nu_tint vec3 1 0.5 0.25\\n\"");

		Loom::Material read;
		Pump();

		REQUIRE(read.GetFields()[VARIABLES].Read(field.Write()));
		CHECK(read.variables->Write() == material.variables->Write());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material samples as its shader says until told otherwise, and saves that last")
	{
		Loom::Material material;
		Pump();

		CHECK(material.sampling == Loom::Material::AsShader);

		material.sampling = Loom::Material::Nearest;
		const std::string written = material.GetFields()[SAMPLING].Write();

		Loom::Material read;
		Pump();

		REQUIRE(read.GetFields()[SAMPLING].Read(written));
		CHECK(read.sampling == Loom::Material::Nearest);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material draws both sides until told otherwise, and saves that last")
	{
		Loom::Material material;
		Pump();

		CHECK(material.sides == Loom::Material::Both);

		material.sides = Loom::Material::Back;
		const std::string written = material.GetFields()[SIDES].Write();

		Loom::Material read;
		Pump();

		REQUIRE(read.GetFields()[SIDES].Read(written));
		CHECK(read.sides == Loom::Material::Back);
	};

	#pragma endregion

	#pragma region Compiling

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a material with no shader named compiles nothing")
	{
		Loom::Scene scene("no shader named");
		Pump();

		Loom::Material* material = scene.Attach<Loom::Material>();
		Pump();

		CHECK(material->shader == nullptr);
	};

#ifndef __EMSCRIPTEN__
	// Native only: a missing path fails in the ifstream here, but needs a browser on the web.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader that failed to compile is tried again")
	{
		const LoomTests::Captured captured(std::cerr);

		{
			Loom::Scene scene("first attempt");
			Pump();

			scene.Attach<Loom::Material>()->SetShaderPath("no such shader.shader");
			Pump();
		};

		{
			Loom::Scene scene("second attempt");
			Pump();

			scene.Attach<Loom::Material>()->SetShaderPath("no such shader.shader");
			Pump();
		};

		// Two materials, two attempts: the second one did not get a remembered
		// failure handed back to it.
		CHECK(captured.Count("Material: no such shader.shader:") == 2);
	};

	// Fixing the file then needs no reload: the editor's poll compiles it
	// again once it has been written, and not before.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a shader that failed to compile is compiled again by the poll once its file changes")
	{
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "loom polled shader.shader";
		std::filesystem::remove(path);

		const std::string failure = "Material: " + path.string() + ":";

		const LoomTests::Captured captured(std::cerr);
		const auto attempts = [&]() { return captured.Count(failure); };

		Loom::Scene scene("polled");
		Pump();

		scene.Attach<Loom::Material>()->SetShaderPath(path.string());
		Pump();
		const size_t attached = attempts();

		Loom::Material::ReloadChangedShaders();
		const size_t unchanged = attempts();

		// Written, and still not a shader that compiles here.
		std::ofstream(path) << "// ===FRAGMENT===\n";

		Loom::Material::ReloadChangedShaders();
		const size_t changed = attempts();

		std::filesystem::remove(path);

		CHECK(attached == 1);
		CHECK(unchanged == 1);
		CHECK(changed == 2);
	};
#endif

	#pragma endregion

	#pragma region Edits

	// Named for no file on disk, so the source edited in is all there is.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an edited shader compiles again from the source given, keeping its program when that does not compile")
	{
		LoomTests::RecordingRenderer renderer;
		const std::string path = "Edited/./edited.shader";
		const std::string spelt = "Edited/edited.shader";

		CHECK(Loom::Material::EditShader(path, VERTEX + Fragment("vec4(1.0)")).empty());

		Loom::Scene scene("edited");
		Loom::Material* material = scene.Attach<Loom::Material>();
		material->SetShaderPath(spelt);
		Pump();

		REQUIRE(material->shader != nullptr);
		const uint32_t first = material->shader->id;

		CHECK(Loom::Material::EditShader(path, VERTEX + Fragment("vec4(0.5)")).empty());
		CHECK(renderer.fragments.at(spelt).find("vec4(0.5)") != std::string::npos);

		const uint32_t second = material->shader->id;
		CHECK(second != first);

		const LoomTests::Captured captured(std::cerr);

		CHECK(Loom::Material::EditShader(path, VERTEX + Fragment(LoomTests::RecordingRenderer::BROKEN)).find("Shader compile error") != std::string::npos);
		CHECK(material->shader->id == second);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an edited pixel shader compiles again from the source given")
	{
		LoomTests::RecordingRenderer renderer;
		const std::string path = "edited.loompixel";
		const std::string pass = "out vec4 FragColor;\nvoid main() { FragColor = LoomScenePixel(LoomScreenPixel()); }\n";

		CHECK(Loom::Material::EditShader(path, pass).empty());

		Loom::Shader* shader = Loom::Material::SharedShader(path);
		REQUIRE(shader != nullptr);

		CHECK(Loom::Material::EditShader(path, pass + "// edited\n").empty());
		CHECK(renderer.fragments.at(path).find("// edited") != std::string::npos);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "editing a PreVertex's file compiles the shaders it covers again with the code given")
	{
		LoomTests::RecordingRenderer renderer;
		const std::string shader = "edited covered.shader";
		const std::string file = "edited.loomprevertex";

		Loom::Material::EditShader(shader, VERTEX + Fragment("vec4(1.0)"));
		Loom::Material::EditShader(file, "vec3 PreVertex(vec3 world) { return world; }\n");

		Loom::Scene scene("edited pre vertex");
		Loom::PreVertex* preVertex = scene.Attach<Loom::PreVertex>();
		preVertex->asset = file;
		preVertex->everything = false;
		preVertex->shaders = { shader };
		Loom::PreVertex::ReloadIfChanged();

		scene.GetRoot().AddChild("Mesh")->Attach<Loom::Material>()->SetShaderPath(shader);
		Pump();

		CHECK(Loom::Material::EditShader(file, "vec3 PreVertex(vec3 world) { return world * 2.0; }\n").empty());
		CHECK(renderer.vertices.at(shader).find("return world * 2.0;") != std::string::npos);

		scene.GetRoot().DetachComponent(preVertex);
		Pump();
	};

	#pragma endregion

	#pragma region Variables

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a script sets and gets a material's variables by name, as its shader declares them")
	{
		LoomTests::RecordingRenderer renderer;
		const std::string path = "Variables/declared.shader";
		const std::string declarations = "uniform float u_wear;\nuniform vec3 u_tint;\nuniform sampler2D u_grain;\n";

		REQUIRE(Loom::Material::EditShader(path, VERTEX + declarations + Fragment("vec4(u_tint * u_wear, 1.0)")).empty());

		Loom::Scene scene("variables");
		Loom::Material* material = scene.Attach<Loom::Material>();
		material->SetShaderPath(path);
		Pump();

		REQUIRE(material->shader != nullptr);
		REQUIRE(material->shader->variables.size() == 3);

		material->Set<"u_wear">(1);
		material->Set<"u_tint">(glm::vec3(1.0f, 0.5f, 0.25f));
		material->Set<"u_grain">("Assets/grain.png");

		CHECK(material->variables->values.at("u_wear").type == Loom::UniformType::Float);

		const float wear = material->Get<"u_wear">();
		const glm::vec3 tint = material->Get<"u_tint">();
		const std::string grain = material->Get<"u_grain">();

		CHECK(wear == 1.0f);
		CHECK(tint == glm::vec3(1.0f, 0.5f, 0.25f));
		CHECK(grain == "Assets/grain.png");
		CHECK(material->variables->Find({ "u_tint", Loom::UniformType::Vec3 }) != nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a variable the shader does not declare keeps the value's own type, and one never set reads as zero")
	{
		Loom::Material material;
		Pump();

		material.Set<"u_cell">(glm::ivec2(2, 3));
		material.Set<"u_turn">(glm::mat2(1.0f, 2.0f, 3.0f, 4.0f));

		CHECK(material.variables->values.at("u_cell").type == Loom::UniformType::IVec2);
		CHECK(material.variables->Write() == "u_cell ivec2 2 3\nu_turn mat2 1 2 3 4\n");

		const glm::ivec2 cell = material.Get<"u_cell">();
		const glm::mat2 turn = material.Get<"u_turn">();
		const glm::mat4 unset = material.Get<"u_unset">();
		const int none = material.Get<"u_unset">();
		const std::string image = material.Get<"u_unset">();

		CHECK(cell == glm::ivec2(2, 3));
		CHECK(turn == glm::mat2(1.0f, 2.0f, 3.0f, 4.0f));
		CHECK(unset == glm::mat4(1.0f));
		CHECK(none == 0);
		CHECK(image.empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a script cannot set a variable as another shape than its shader declares, or one its meshes hold")
	{
		LoomTests::RecordingRenderer renderer;
		const std::string path = "Variables/refused.shader";
		const std::string declarations = "uniform vec3 u_tint;\nuniform float u_speed; // instance\n";

		REQUIRE(Loom::Material::EditShader(path, VERTEX + declarations + Fragment("vec4(u_tint * u_speed, 1.0)")).empty());

		Loom::Scene scene("refused");
		Loom::Material* material = scene.Attach<Loom::Material>();
		material->SetShaderPath(path);
		Pump();

		REQUIRE(material->shader != nullptr);
		REQUIRE(material->shader->variables.size() == 2);

		material->Set<"u_tint">(glm::vec3(1.0f, 0.5f, 0.25f));
		material->Set<"u_tint">(1.0f);
		material->Set<"u_speed">(2.0f);

		const glm::vec3 tint = material->Get<"u_tint">();

		CHECK(tint == glm::vec3(1.0f, 0.5f, 0.25f));
		CHECK(material->variables->values.count("u_speed") == 0);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a value set on an integer or bool variable is held as the shader takes it, read back and saved alike")
	{
		LoomTests::RecordingRenderer renderer;
		const std::string path = "Variables/held.shader";
		const std::string declarations = "uniform int u_count;\nuniform uint u_mask;\nuniform bool u_flag;\nuniform ivec2 u_cell;\n";

		REQUIRE(Loom::Material::EditShader(path, VERTEX + declarations + Fragment("vec4(1.0)")).empty());

		Loom::Scene scene("held");
		Loom::Material* material = scene.Attach<Loom::Material>();
		material->SetShaderPath(path);
		Pump();

		REQUIRE(material->shader != nullptr);

		material->Set<"u_count">(1.5f);
		material->Set<"u_mask">(-1);
		material->Set<"u_flag">(0.5f);
		material->Set<"u_cell">(glm::vec2(-1.5f, 2.5f));

		constexpr uint32_t ALL_BITS = 0xFFFFFFFF;

		const float count = material->Get<"u_count">();
		const uint32_t mask = material->Get<"u_mask">();
		const float flag = material->Get<"u_flag">();
		const glm::ivec2 cell = material->Get<"u_cell">();

		CHECK(count == 1.0f);
		CHECK(mask == ALL_BITS);
		CHECK(flag == 1.0f);
		CHECK(cell == glm::ivec2(-1, 2));

		Loom::UniformValues saved;
		saved.Read(material->variables->Write());

		CHECK(saved == *material->variables);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a script sets a variable straight from its own fields")
	{
		Loom::Material material;
		const Paint paint;
		Pump();

		material.Set<"u_wear">(paint.wear);
		material.Set<"u_tint">(paint.tint);
		material.Set<"u_grain">(paint.image);

		const float wear = material.Get<"u_wear">();
		const glm::vec3 tint = material.Get<"u_tint">();
		const std::string grain = material.Get<"u_grain">();

		CHECK(wear == 0.5f);
		CHECK(tint == glm::vec3(1.0f, 0.5f, 0.25f));
		CHECK(grain == "Assets/grain.png");
	};

	#pragma endregion
};
