#include "doctest.h"

#include "Test Support.h"

#include "ModelEditor.h"
#include "Prefabs.h"

#include "GameObject.h"
#include "Material.h"
#include "Mesh.h"
#include "Scene.h"
#include "SceneSerializer.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>


namespace
{
	using Loom::EditableMesh;
	using Loom::ModelEditor;

	constexpr float TOLERANCE = 1e-4f;

	std::string ReadFile(const std::string& path)
	{
		std::ifstream file(path, std::ios::binary);
		std::stringstream text;
		text << file.rdbuf();

		return text.str();
	};

	// A prefab of one object, drawn with a mesh of the shape given.
	std::string WritePrefab(const std::string& name, const EditableMesh& shape)
	{
		const std::string path = (std::filesystem::temp_directory_path() / (name + ".loomprefab")).string();

		Loom::Scene source("Source");
		LoomTests::Pump();

		Loom::GameObject* crate = source.AddChild("Crate");
		crate->Attach<Loom::Material>()->SetShaderPath("Assets/Shader.shader");

		std::vector<float> positions{ };
		std::vector<float> uvs{ };
		std::vector<float> normals{ };
		shape.ToTriangles(positions, uvs, normals);

		Loom::Mesh* mesh = crate->Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		mesh->m_vertices = positions;
		mesh->m_textureCoordinates = uvs;
		mesh->m_normals = normals;
		LoomTests::Pump();

		std::ofstream(path, std::ios::binary) << Loom::SceneSerializer::Serialize(*crate);

		return path;
	};

	// The Model panel in an ImGui context of the test's own, with a renderer
	// that only records, filling the display with a unit cube's prefab open.
	struct Driven : LoomTests::EngineFixture
	{
		static constexpr float width = 1000.0f;
		static constexpr float height = 700.0f;

		// Enough for the panel to dock its tab, take focus and size its view.
		static constexpr int opening_frames = 4;

		explicit Driven(const std::string& name) :
			path(WritePrefab(name, EditableMesh::Cube(1.0f)))
		{
			constexpr float frame_time = 1.0f / 60.0f;

			context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(width, height);
			io.DeltaTime = frame_time;
			io.ConfigInputTrickleEventQueue = false;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);

			// The engine's objects start once the queue next runs, which the
			// editor's frame does.
			editor.Open(path);
			LoomTests::Pump();

			for (int i = 0; i < opening_frames; i++)
				Frame();
		};

		~Driven()
		{
			ImGui::DestroyContext(context);

			std::error_code code;
			std::filesystem::remove(path, code);
		};

		void Frame()
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(width, height));
			editor.Draw(&open);
			ImGui::EndFrame();
		};

		// Presses at from, moves to to, and lets go.
		void Drag(const ImVec2& from, const ImVec2& to)
		{
			ImGuiIO& io = ImGui::GetIO();

			io.AddMousePosEvent(from.x, from.y);
			Frame();

			io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			Frame();

			io.AddMousePosEvent(to.x, to.y);
			Frame();
			Frame();

			io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Frame();
		};

		void Click(const ImVec2& at)
		{
			Drag(at, at);
		};

		ImVec2 Screen(const glm::vec3& point) const
		{
			const std::optional<ImVec2> found = editor.ScreenOf(point);
			REQUIRE(found);

			return *found;
		};

		const EditableMesh& Mesh() const
		{
			const EditableMesh* mesh = editor.Editing();
			REQUIRE(mesh != nullptr);

			return *mesh;
		};

		ImGuiContext* context = nullptr;
		LoomTests::RecordingRenderer renderer;
		std::string path;
		ModelEditor editor;
		bool open = true;
	};
};

TEST_CASE("The Model panel opens a prefab's mesh with its triangles welded back into quads")
{
	Driven driven("model_opens");

	CHECK(driven.Mesh().vertices.size() == 8);
	CHECK(driven.Mesh().faces.size() == 6);
};

TEST_CASE("The Model panel picks the face clicked, and nothing when the click misses")
{
	Driven driven("model_click");

	driven.editor.SetPick(ModelEditor::Pick::Faces);
	driven.Click(driven.Screen(glm::vec3(0.1f, 0.5f, 0.1f)));

	REQUIRE(driven.editor.Selected()->faces.size() == 1);

	const uint32_t face = *driven.editor.Selected()->faces.begin();
	CHECK(driven.Mesh().FaceNormal(face).y > 1.0f - TOLERANCE);

	driven.Click(ImVec2(Driven::width - 10.0f, Driven::height - 40.0f));
	CHECK(driven.editor.Selected()->faces.empty());
};

TEST_CASE("The Model panel's box picks the corners it covers that can be seen")
{
	Driven driven("model_box");

	driven.editor.SetPick(ModelEditor::Pick::Vertices);

	ImVec2 low(Driven::width, Driven::height);
	ImVec2 high(0.0f, 0.0f);

	for (const glm::vec3& vertex : driven.Mesh().vertices)
	{
		const ImVec2 point = driven.Screen(vertex);
		low = ImVec2((std::min)(low.x, point.x), (std::min)(low.y, point.y));
		high = ImVec2((std::max)(high.x, point.x), (std::max)(high.y, point.y));
	};

	constexpr float margin = 20.0f;
	driven.Drag(ImVec2(low.x - margin, low.y - margin), ImVec2(high.x + margin, high.y + margin));

	// The camera looks down from the +X +Z side, so the far bottom corner is
	// hidden behind the cube.
	CHECK(driven.editor.Selected()->vertices.size() == 7);
};

TEST_CASE("The Model panel's move arrow lifts the picked face, and undo puts it back")
{
	Driven driven("model_move");

	driven.editor.SetPick(ModelEditor::Pick::Faces);
	driven.Click(driven.Screen(glm::vec3(0.1f, 0.5f, 0.1f)));
	REQUIRE(driven.editor.Selected()->faces.size() == 1);

	const uint32_t face = *driven.editor.Selected()->faces.begin();
	const glm::vec3 centre = driven.Mesh().FaceCentre(face);

	// Partway along the Y arrow, then further up the screen along it.
	const ImVec2 middle = driven.Screen(centre);
	const ImVec2 above = driven.Screen(centre + glm::vec3(0.0f, 0.1f, 0.0f));
	const float reach = std::hypot(above.x - middle.x, above.y - middle.y);
	const ImVec2 up((above.x - middle.x) / reach, (above.y - middle.y) / reach);

	constexpr float grab = 60.0f;
	constexpr float pull = 40.0f;

	driven.Drag(ImVec2(middle.x + up.x * grab, middle.y + up.y * grab), ImVec2(middle.x + up.x * (grab + pull), middle.y + up.y * (grab + pull)));

	const glm::vec3 moved = driven.Mesh().FaceCentre(face);
	CHECK(moved.y > centre.y + TOLERANCE);
	CHECK(std::abs(moved.x - centre.x) < TOLERANCE);
	CHECK(std::abs(moved.z - centre.z) < TOLERANCE);

	driven.editor.Undo();
	CHECK(std::abs(driven.Mesh().FaceCentre(face).y - centre.y) < TOLERANCE);

	driven.editor.Redo();
	CHECK(std::abs(driven.Mesh().FaceCentre(face).y - moved.y) < TOLERANCE);
};

TEST_CASE("The Model panel's middle handles size and turn the picked face about its middle")
{
	// How far the middle handles are dragged, in screen pixels: right and up
	// to size, and a quarter of the way round the outer ring to turn.
	constexpr float pull = 40.0f;
	constexpr float ring = 112.5f;

	Driven driven("model_size_turn");

	driven.editor.SetPick(ModelEditor::Pick::Faces);
	driven.Click(driven.Screen(glm::vec3(0.1f, 0.5f, 0.1f)));
	REQUIRE(driven.editor.Selected()->faces.size() == 1);

	const uint32_t face = *driven.editor.Selected()->faces.begin();
	const glm::vec3 centre = driven.Mesh().FaceCentre(face);
	const auto side = [&]()
	{
		const std::vector<EditableMesh::Corner>& corners = driven.Mesh().faces[face].corners;
		return glm::distance(driven.Mesh().vertices[corners[0].vertex], driven.Mesh().vertices[corners[1].vertex]);
	};

	const ImVec2 middle = driven.Screen(centre);

	driven.editor.SetTool(ModelEditor::Tool::Scale);
	driven.Drag(middle, ImVec2(middle.x + pull, middle.y - pull));

	const float sized = side();
	CHECK(sized > 1.5f);
	CHECK(glm::distance(driven.Mesh().FaceCentre(face), centre) < TOLERANCE);

	const glm::vec3 corner = driven.Mesh().vertices[driven.Mesh().faces[face].corners[0].vertex];

	driven.editor.SetTool(ModelEditor::Tool::Rotate);
	driven.Drag(ImVec2(middle.x + ring, middle.y), ImVec2(middle.x, middle.y - ring));

	CHECK(std::abs(side() - sized) < TOLERANCE);
	CHECK(glm::distance(driven.Mesh().FaceCentre(face), centre) < TOLERANCE);
	CHECK(glm::distance(driven.Mesh().vertices[driven.Mesh().faces[face].corners[0].vertex], corner) > 0.1f);
};

TEST_CASE("The Model panel saves an extrusion into the prefab, keeping its guids")
{
	Driven driven("model_save");

	Loom::Prefabs::ObjectNode before;
	REQUIRE(Loom::Prefabs::Parse(ReadFile(driven.path), before));

	driven.editor.SetPick(ModelEditor::Pick::Faces);
	driven.Click(driven.Screen(glm::vec3(0.1f, 0.5f, 0.1f)));
	REQUIRE(driven.editor.Selected()->faces.size() == 1);

	driven.editor.Extrude();
	REQUIRE(driven.editor.Save());

	Loom::Prefabs::ObjectNode after;
	REQUIRE(Loom::Prefabs::Parse(ReadFile(driven.path), after));
	CHECK(after.guid == before.guid);
	REQUIRE(after.components.size() == before.components.size());

	for (size_t i = 0; i < after.components.size(); i++)
		CHECK(after.components[i].guid == before.components[i].guid);

	// Opened again, the extruded triangle stands above the cube's top.
	ModelEditor reopened;
	reopened.Open(driven.path);
	LoomTests::Pump();

	const EditableMesh* mesh = reopened.Editing();
	REQUIRE(mesh != nullptr);
	CHECK(mesh->Bounds().second.y > 0.5f + TOLERANCE);
};

TEST_CASE("The Model panel adds a shape, deletes what is picked, and steps back through both")
{
	Driven driven("model_add");

	const size_t faces = driven.Mesh().faces.size();

	driven.editor.Add(ModelEditor::Shape::Cylinder);
	const size_t added = driven.editor.Selected()->faces.size();
	CHECK(added > 0);
	CHECK(driven.Mesh().faces.size() == faces + added);

	driven.editor.Delete();
	CHECK(driven.Mesh().faces.size() == faces);
	CHECK(driven.Mesh().vertices.size() == 8);

	driven.editor.Undo();
	CHECK(driven.Mesh().faces.size() == faces + added);

	driven.editor.Undo();
	CHECK(driven.Mesh().faces.size() == faces);
};

TEST_CASE("The Model panel reads a prefab changed elsewhere again, unless it has edits of its own")
{
	Driven driven("model_reload");

	std::ofstream(driven.path, std::ios::binary) << ReadFile(WritePrefab("model_reload_plane", EditableMesh::Plane(1.0f)));
	driven.Frame();

	CHECK(driven.Mesh().faces.size() == 1);

	driven.editor.Add(ModelEditor::Shape::Cube);
	std::ofstream(driven.path, std::ios::binary) << ReadFile(WritePrefab("model_reload_cube", EditableMesh::Cube(1.0f)));
	driven.Frame();

	CHECK(driven.Mesh().faces.size() == 7);
};

TEST_CASE("The Model panel's save keeps what it could not read of the prefab")
{
	Driven driven("model_keeps");

	// A component no build has, as a script this editor has not loaded would be.
	constexpr const char* MYSTERY = "\tComponent Mystery(6c1a4d55-0d3e-4b8e-9a31-6b2a1f0e7c42)\n\t\t0 = 1\n";

	std::ofstream(driven.path, std::ios::binary | std::ios::app) << MYSTERY;
	driven.Frame();

	driven.editor.Add(ModelEditor::Shape::Cube);
	REQUIRE(driven.editor.Save());

	const std::string saved = ReadFile(driven.path);
	CHECK(saved.find("Component Mystery(6c1a4d55-0d3e-4b8e-9a31-6b2a1f0e7c42)") != std::string::npos);

	ModelEditor reopened;
	reopened.Open(driven.path);
	LoomTests::Pump();

	REQUIRE(reopened.Editing() != nullptr);
	CHECK(reopened.Editing()->faces.size() == 12);
};
