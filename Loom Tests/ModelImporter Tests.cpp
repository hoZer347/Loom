#ifndef __EMSCRIPTEN__
#include "doctest.h"

#include "Test Support.h"

#include "McpProtocol.h"
#include "ModelImporter.h"

#include "GameObject.h"
#include "Material.h"
#include "Mesh.h"
#include "Scene.h"

#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace
{
	constexpr int GLTF_FLOAT = 5126;
	constexpr int GLTF_NEAREST = 9728;
	constexpr float CUTOFF = 0.3f;

	// One texel, so the texture the materials name has an image.
	constexpr const char* TEXEL_PNG = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=";

	const std::vector<float> POSITIONS = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
	const std::vector<float> NORMALS = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
	const std::vector<float> UVS = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
	const std::vector<float> SECOND_UVS = { 0.25f, 0.5f, 0.75f, 1.0f, -1.0f, 2.0f };

	// SECOND_UVS as imported: glTF counts V down the texture, and every set
	// of coordinates comes in counting it up, as aUV does.
	const std::vector<float> SECOND_UVS_IMPORTED = { 0.25f, 0.5f, 0.75f, 0.0f, -1.0f, -1.0f };
	const std::vector<float> COLORS = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.5f };
	const std::vector<float> TIMES = { 0.0f, 1.0f };
	const std::vector<float> STEPS = { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };

	/**
	* A glTF of two triangles sharing one buffer, written to path:
	* - "Painted", coloured by its corners, with a second set of texture
	*   coordinates, in a half blue material with a texture sampled nearest
	*   and cut out below CUTOFF
	* - "Glowing", in a material drawn by its emission's texture alone
	* - Animated, moving "Painted", which makes the importer rig it
	*/
	void WriteModel(const std::filesystem::path& path, bool animated)
	{
		std::vector<char> buffer;
		json views = json::array();
		json accessors = json::array();

		const auto add = [&](const std::vector<float>& floats, const char* type, size_t count)
		{
			const size_t offset = buffer.size();
			buffer.resize(offset + floats.size() * sizeof(float));
			std::memcpy(buffer.data() + offset, floats.data(), floats.size() * sizeof(float));

			views.push_back(json{ { "buffer", 0 }, { "byteOffset", offset }, { "byteLength", floats.size() * sizeof(float) } });
			accessors.push_back(json{ { "bufferView", views.size() - 1 }, { "componentType", GLTF_FLOAT }, { "count", count }, { "type", type } });

			return accessors.size() - 1;
		};

		const auto primitive = [](const json& attributes, int material)
		{
			return json{ { "primitives", json::array({ json{ { "attributes", attributes }, { "material", material } } }) } };
		};

		const size_t corners = POSITIONS.size() / 3;
		const size_t positions = add(POSITIONS, "VEC3", corners);
		accessors[positions]["min"] = json::array({ 0.0f, 0.0f, 0.0f });
		accessors[positions]["max"] = json::array({ 1.0f, 1.0f, 0.0f });

		const json plain = json{ { "POSITION", positions }, { "NORMAL", add(NORMALS, "VEC3", corners) }, { "TEXCOORD_0", add(UVS, "VEC2", corners) } };
		json painted = plain;
		painted["COLOR_0"] = add(COLORS, "VEC4", corners);
		painted["TEXCOORD_1"] = add(SECOND_UVS, "VEC2", corners);

		const json texture = json{ { "index", 0 } };

		json model = json::object();
		model["asset"] = json{ { "version", "2.0" } };
		model["scene"] = 0;
		model["scenes"] = json::array({ json{ { "nodes", json::array({ 0, 1 }) } } });
		model["nodes"] = json::array({ json{ { "name", "Painted" }, { "mesh", 0 } }, json{ { "name", "Glowing" }, { "mesh", 1 } } });
		model["meshes"] = json::array({ primitive(painted, 0), primitive(plain, 1) });
		model["materials"] = json::array({
			json{
				{ "alphaMode", "MASK" },
				{ "alphaCutoff", CUTOFF },
				{ "pbrMetallicRoughness", json{ { "baseColorFactor", json::array({ 0.5f, 0.5f, 1.0f, 1.0f }) }, { "baseColorTexture", texture } } } },
			json{
				{ "pbrMetallicRoughness", json{ { "baseColorFactor", json::array({ 0.0f, 0.0f, 0.0f, 1.0f }) } } },
				{ "emissiveFactor", json::array({ 1.0f, 0.5f, 0.25f }) },
				{ "emissiveTexture", texture } } });
		model["textures"] = json::array({ json{ { "source", 0 }, { "sampler", 0 } } });
		model["samplers"] = json::array({ json{ { "magFilter", GLTF_NEAREST }, { "minFilter", GLTF_NEAREST } } });
		model["images"] = json::array({ json{ { "uri", std::string("data:image/png;base64,") + TEXEL_PNG } } });

		if (animated)
		{
			const size_t times = add(TIMES, "SCALAR", TIMES.size());
			accessors[times]["min"] = json::array({ TIMES.front() });
			accessors[times]["max"] = json::array({ TIMES.back() });

			const json sampler = json{ { "input", times }, { "output", add(STEPS, "VEC3", TIMES.size()) } };
			const json channel = json{ { "sampler", 0 }, { "target", json{ { "node", 0 }, { "path", "translation" } } } };

			model["animations"] = json::array({ json{ { "samplers", json::array({ sampler }) }, { "channels", json::array({ channel }) } } });
		};

		model["bufferViews"] = views;
		model["accessors"] = accessors;
		model["buffers"] = json::array({ json{ { "byteLength", buffer.size() }, { "uri", "data:application/octet-stream;base64," + Loom::McpProtocol::Base64(buffer) } } });

		std::ofstream(path) << model.dump();
	};

	Loom::GameObject* Named(Loom::GameObject& object, const std::string& name)
	{
		if (object.GetName() == name)
			return &object;

		for (Loom::GameObject* child : object.GetChildren())
			if (Loom::GameObject* found = Named(*child, name))
				return found;

		return nullptr;
	};

	// Each part drawn as the file's material says: Model.shader, the base
	// colour, the texture copied out, the vertex colours, the second texture
	// coordinates as UV_1 and the cutout, or for the glowing part its
	// emission's colour and texture.
	void CheckDressed(Loom::GameObject& model)
	{
		Loom::GameObject* painted = Named(model, "Painted");
		Loom::GameObject* glowing = Named(model, "Glowing");
		REQUIRE(painted != nullptr);
		REQUIRE(glowing != nullptr);

		const Loom::ShaderVariable texture{ Loom::model_texture_variable, Loom::UniformType::Sampler2D };
		const Loom::ShaderVariable cutoff{ Loom::model_alpha_cutoff_variable, Loom::UniformType::Float };

		Loom::Material* paint = painted->GetComponent<Loom::Material>();
		REQUIRE(paint != nullptr);
		CHECK(paint->GetShaderPath() == Loom::model_shader_path);
		CHECK(*paint->color == glm::vec3(0.5f, 0.5f, 1.0f));
		CHECK(*paint->sampling == Loom::Material::Nearest);
		REQUIRE(paint->variables->Find(cutoff) != nullptr);
		CHECK(paint->variables->Find(cutoff)->components[0] == doctest::Approx(CUTOFF));
		REQUIRE(paint->variables->Find(texture) != nullptr);
		CHECK(std::filesystem::exists(paint->variables->Find(texture)->texture));
		CHECK(*painted->GetComponent<Loom::Mesh>()->m_colors == COLORS);

		const std::vector<Loom::UVLayer> layers = painted->GetComponent<Loom::Mesh>()->UVLayers();
		REQUIRE(layers.size() == 1);
		CHECK(layers[0].id == 1);
		CHECK(layers[0].words == 2);
		REQUIRE(layers[0].values.size() == SECOND_UVS_IMPORTED.size());

		for (size_t i = 0; i < SECOND_UVS_IMPORTED.size(); i++)
			CHECK(std::bit_cast<float>(layers[0].values[i]) == SECOND_UVS_IMPORTED[i]);

		Loom::Material* glow = glowing->GetComponent<Loom::Material>();
		REQUIRE(glow != nullptr);
		CHECK(glow->GetShaderPath() == Loom::model_shader_path);
		CHECK(*glow->color == glm::vec3(1.0f, 0.5f, 0.25f));
		CHECK(glow->variables->Find(cutoff) == nullptr);
		REQUIRE(glow->variables->Find(texture) != nullptr);
		CHECK(std::filesystem::exists(glow->variables->Find(texture)->texture));
		CHECK(glowing->GetComponent<Loom::Mesh>()->m_colors->empty());
		CHECK(glowing->GetComponent<Loom::Mesh>()->UVLayers().empty());
	};

	// The importer writes Model.shader and the textures into the project it
	// runs in, so a case runs in a folder of its own until it ends.
	struct InFolder
	{
		std::filesystem::path was = std::filesystem::current_path();

		explicit InFolder(const std::filesystem::path& folder)
		{
			std::error_code code;
			std::filesystem::remove_all(folder, code);
			std::filesystem::create_directories(folder);
			std::filesystem::current_path(folder);
		};

		~InFolder() { std::filesystem::current_path(was); };
	};

	// One triangle whose last corner is the vertex given.
	std::string Triangle(int last)
	{
		return
			"ply\n"
			"format ascii 1.0\n"
			"element vertex 3\n"
			"property float x\n"
			"property float y\n"
			"property float z\n"
			"element face 1\n"
			"property list uchar int vertex_indices\n"
			"end_header\n"
			"0 0 0\n"
			"1 0 0\n"
			"0 1 0\n"
			"3 0 1 " + std::to_string(last) + "\n";
	};

	std::vector<Loom::ModelPart> Load(const std::string& text)
	{
		const std::string path = (std::filesystem::temp_directory_path() / "loom model.ply").string();

		{
			std::ofstream file(path);
			file << text;
		};

		std::vector<Loom::ModelPart> parts = Loom::LoadModel(path);
		std::filesystem::remove(path);

		return parts;
	};
};

TEST_SUITE("ModelImporter")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an imported model draws each part in its material, still or rigged")
	{
		const std::filesystem::path project = std::filesystem::temp_directory_path() / "model_import";
		bool animated = false;

		SUBCASE("still") { };
		SUBCASE("rigged") { animated = true; };

		{
			const InFolder in(project);
			WriteModel(project / "Swatch.gltf", animated);

			// No renderer compiles the shader the importer writes.
			const LoomTests::Captured captured(std::cerr);
			Loom::Scene scene("imported");
			LoomTests::Pump();

			Loom::GameObject* model = Loom::ImportModel((project / "Swatch.gltf").string(), scene.GetRoot());
			REQUIRE(model != nullptr);
			LoomTests::Pump();

			CHECK(std::filesystem::exists(Loom::model_shader_path));
			CheckDressed(*model);
			CHECK(captured.Says("No renderer to compile"));
		};

		std::error_code code;
		std::filesystem::remove_all(project, code);
	};

	TEST_CASE("a model whose faces name vertices it does not have is refused, and one naming its own loads")
	{
		constexpr int last = 2;
		constexpr int missing = 99;

		const std::vector<Loom::ModelPart> whole = Load(Triangle(last));

		REQUIRE(whole.size() == 1);
		CHECK(whole[0].vertices.size() == Loom::model_floats_per_vertex * Loom::model_vertices_per_triangle);

		const LoomTests::Captured captured(std::cerr);

		CHECK(Load(Triangle(missing)).empty());
		CHECK(captured.Says("loom model.ply: Validation failed"));
	};
};
#endif
