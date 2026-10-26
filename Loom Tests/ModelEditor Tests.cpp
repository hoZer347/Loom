#include "doctest.h"

#include "Test Support.h"

#include "ModelEditor.h"
#include "Prefabs.h"
#include "UiScript.h"

#include "GameObject.h"
#include "Material.h"
#include "Mesh.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "Skin.h"

#include "imgui.h"

#include "glm/gtc/matrix_transform.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>


namespace
{
	using Loom::EditableMesh;
	using Loom::ModelEditor;
	namespace Shapes = Loom::Shapes;

	constexpr float TOLERANCE = 1e-4f;

	std::string ReadFile(const std::string& path)
	{
		std::ifstream file(path, std::ios::binary);
		std::stringstream text;
		text << file.rdbuf();

		return text.str();
	};

	void Draw(Loom::GameObject& object, const EditableMesh& shape)
	{
		// With no shader, which the web build would fetch, and node cannot.
		object.Attach<Loom::Material>();

		std::vector<float> positions{ };
		std::vector<float> uvs{ };
		std::vector<float> normals{ };
		std::vector<float> colors{ };
		shape.ToTriangles(positions, uvs, normals, colors);

		Loom::Mesh* mesh = object.Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		mesh->m_vertices = positions;
		mesh->m_textureCoordinates = uvs;
		mesh->m_normals = normals;
		mesh->m_colors = colors;
		mesh->SetUVLayers(shape.ToUVLayers());
	};

	// A prefab drawn with a mesh of the shape given on its top object, and
	// any child's shape on a child of it, animated by a Skin if skinned.
	std::string WritePrefab(const std::string& name, const EditableMesh* shape, const EditableMesh* child = nullptr, bool skinned = false)
	{
		const std::string path = (std::filesystem::temp_directory_path() / (name + ".loomprefab")).string();

		Loom::Scene source("Source");
		LoomTests::Pump();

		Loom::GameObject* top = source.AddChild("Crate");

		if (shape)
			Draw(*top, *shape);

		if (child)
		{
			Loom::GameObject* lid = top->AddChild("Lid");
			Draw(*lid, *child);

			if (skinned)
				lid->Attach<Loom::Skin>();
		};

		LoomTests::Pump();

		std::ofstream(path, std::ios::binary) << Loom::SceneSerializer::Serialize(*top);

		return path;
	};

	// A crate whose skinned lid is bent by Bones' two joints, Hinge and Tip
	// above it, every corner by Tip.
	std::string WriteJointedPrefab(const std::string& name)
	{
		constexpr size_t joints_per_vertex = 4;

		const EditableMesh cube = EditableMesh::Cube(1.0f);
		const std::string path = WritePrefab(name, nullptr, &cube, true);

		Loom::Scene source("Source");
		LoomTests::Pump();

		Loom::GameObject* top = Loom::SceneSerializer::Deserialize(ReadFile(path), source.GetRoot());
		LoomTests::Pump();

		Loom::GameObject* bones = top->AddChild("Bones");
		Loom::GameObject* hinge = bones->AddChild("Hinge");
		Loom::GameObject* tip = hinge->AddChild("Tip");
		tip->transform.position = glm::vec3(0.0f, 1.0f, 0.0f);
		LoomTests::Pump();

		Loom::GameObject* lid = top->GetChildren().front();
		Loom::Skin* skin = lid->GetComponent<Loom::Skin>();
		skin->skeleton = bones;
		skin->m_joints = "Hinge\nTip\n";

		std::vector<float> binds{ };

		for (const glm::mat4& bind : { glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f)) })
			for (int i = 0; i < 4 * 4; i++)
				binds.push_back(bind[i / 4][i % 4]);

		skin->m_inverseBinds = binds;

		std::vector<float> influences{ };
		const size_t vertices = (*std::as_const(lid->GetComponent<Loom::Mesh>()->m_vertices)).size() / 3;

		for (size_t i = 0; i < vertices; i++)
		{
			influences.push_back(1.0f);
			influences.insert(influences.end(), joints_per_vertex - 1, 0.0f);
			influences.push_back(1.0f);
			influences.insert(influences.end(), joints_per_vertex - 1, 0.0f);
		};

		skin->m_influences = influences;
		LoomTests::Pump();

		std::ofstream(path, std::ios::binary) << Loom::SceneSerializer::Serialize(*top);

		return path;
	};

	Loom::GameObject* Named(Loom::GameObject& root, const std::string& name)
	{
		Loom::GameObject* found = Loom::DescendantsByName(root)[name];
		REQUIRE(found != nullptr);

		return found;
	};

	glm::vec3 Placed(const Loom::GameObject& object)
	{
		return glm::vec3(object.WorldMatrix()[3]);
	};

	// The Model panel in an ImGui context of the test's own, with a renderer
	// that only records, filling the display with a unit cube's prefab open.
	struct Driven : LoomTests::EngineFixture
	{
		static constexpr float width = 1000.0f;
		static constexpr float height = 700.0f;

		// Enough for the panel to dock its tab, take focus and size its view.
		static constexpr int opening_frames = 4;

		// The prefab at file, or a cube's with any child's shape under it. With
		// text, file is not written yet and opens as that.
		explicit Driven(const std::string& name, const EditableMesh* child = nullptr, const std::string& file = "", const std::string& text = "") :
			path(file.empty() ? WritePrefab(name, &cube, child) : file)
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
			editor.Open(path, text);
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

		// Frames until a UiScript of text has run, aiming at what the panel draws.
		void Run(const std::string& text)
		{
			constexpr int most_frames = 600;

			Loom::UiScript script{ std::filesystem::path() };
			script.LoadText(text);

			for (int frame = 0; frame < most_frames && !script.Finished(); frame++)
			{
				script.Step();
				Frame();
			};

			CHECK_FALSE(script.Failed());
		};

		void Key(ImGuiKey key)
		{
			ImGuiIO& io = ImGui::GetIO();

			io.AddKeyEvent(key, true);
			Frame();

			io.AddKeyEvent(key, false);
			Frame();
		};

		// The corners of a box round every vertex of the mesh being edited, a
		// little way out: the top left, or the bottom right.
		ImVec2 Around(bool far = false) const
		{
			constexpr float margin = 20.0f;

			ImVec2 low(width, height);
			ImVec2 high(0.0f, 0.0f);

			for (const glm::vec3& vertex : Mesh().vertices)
			{
				const ImVec2 point = Screen(vertex);
				low = ImVec2((std::min)(low.x, point.x), (std::min)(low.y, point.y));
				high = ImVec2((std::max)(high.x, point.x), (std::max)(high.y, point.y));
			};

			return far ? ImVec2(high.x + margin, high.y + margin) : ImVec2(low.x - margin, low.y - margin);
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

		const EditableMesh cube = EditableMesh::Cube(1.0f);

		ImGuiContext* context = nullptr;
		LoomTests::RecordingRenderer renderer;
		std::string path;
		ModelEditor editor;
		bool open = true;
	};
};

TEST_SUITE("ModelEditor")
{
	TEST_CASE("The Model panel opens a prefab's mesh with its triangles welded back into quads")
	{
		Driven driven("model_opens");

		CHECK(driven.Mesh().vertices.size() == 8);
		CHECK(driven.Mesh().faces.size() == 6);
	};

	TEST_CASE("The Model panel finds a mesh on a child of the prefab's top object")
	{
		LoomTests::EngineFixture engine;

		const EditableMesh cube = EditableMesh::Cube(1.0f);
		const std::string path = WritePrefab("model_child", nullptr, &cube);

		ModelEditor editor;
		editor.Open(path);
		LoomTests::Pump();

		REQUIRE(editor.Editing() != nullptr);
		CHECK(editor.Editing()->faces.size() == 6);

		std::error_code code;
		std::filesystem::remove(path, code);
	};

	TEST_CASE("The Model panel opens a prefab not yet written, writes it where it was told to once saved, and goes by the file from then on")
	{
		std::string text;
		std::error_code code;

		{
			LoomTests::EngineFixture engine;

			const EditableMesh cube = EditableMesh::Cube(1.0f);
			const std::string source = WritePrefab("model_unwritten_source", &cube);
			text = ReadFile(source);

			std::filesystem::remove(source, code);
		};

		const std::string path = (std::filesystem::temp_directory_path() / "model_unwritten.loomprefab").string();
		std::filesystem::remove(path, code);

		Driven driven("model_unwritten", nullptr, path, text);

		CHECK(driven.Mesh().faces.size() == 6);
		CHECK_FALSE(std::filesystem::exists(path));

		REQUIRE(driven.editor.Save());
		REQUIRE(std::filesystem::exists(path));

		// Deleted once written, it is gone rather than back to the text it opened as.
		const LoomTests::Captured captured(std::cerr);
		std::filesystem::remove(path, code);

		driven.Frame();
		driven.Frame();

		CHECK(driven.editor.Editing() == nullptr);
		CHECK(captured.Says(path));
	};

	TEST_CASE("The Model panel goes on to edit the part whose face is clicked")
	{
		// A lid floating a cube's height above it.
		EditableMesh lid = EditableMesh::Plane(1.0f);
		lid.Transform({ 0, 1, 2, 3 }, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)));

		Driven driven("model_parts", &lid);
		REQUIRE(driven.Mesh().faces.size() == 6);

		driven.editor.SetPick(ModelEditor::Pick::Faces);
		driven.Click(driven.Screen(glm::vec3(0.1f, 1.0f, 0.1f)));

		CHECK(driven.Mesh().faces.size() == 1);
		CHECK(driven.editor.Selected()->faces == std::set<uint32_t>{ 0 });
	};

	TEST_CASE("The Model panel's box leaves out corners another part hides")
	{
		// A lid just above the cube and wider than it, which hides the cube's
		// top from the camera looking down.
		constexpr float lid_height = 0.6f;
		constexpr float lid_size = 3.0f;

		EditableMesh lid = EditableMesh::Plane(lid_size);
		lid.Transform({ 0, 1, 2, 3 }, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, lid_height, 0.0f)));

		Driven driven("model_hidden", &lid);
		REQUIRE(driven.Mesh().faces.size() == 6);

		driven.editor.SetPick(ModelEditor::Pick::Vertices);
		driven.Drag(driven.Around(), driven.Around(true));

		// The bottom corners the cube itself leaves in view are still picked.
		REQUIRE_FALSE(driven.editor.Selected()->vertices.empty());

		for (const uint32_t vertex : driven.editor.Selected()->vertices)
			CHECK(driven.Mesh().vertices[vertex].y < 0.0f);
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
		driven.Drag(driven.Around(), driven.Around(true));

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

	TEST_CASE("The Model panel saves an extrusion's walls with the UV layer words of the corners they rise from")
	{
		constexpr uint32_t LAYER = 1;
		constexpr size_t POSITION_COMPONENTS = 3;
		constexpr size_t WALLS = 4;
		constexpr size_t CORNERS_PER_WALL = 6;

		// A cube whose layer holds each corner's height as its word.
		const EditableMesh cube = EditableMesh::Cube(1.0f);
		std::vector<float> positions{ };
		std::vector<float> uvs{ };
		std::vector<float> normals{ };
		std::vector<float> colors{ };
		cube.ToTriangles(positions, uvs, normals, colors);

		Loom::UVLayer heights{ LAYER, 1, { } };

		for (size_t i = 1; i < positions.size(); i += POSITION_COMPONENTS)
			heights.values.push_back(std::bit_cast<uint32_t>(positions[i]));

		const EditableMesh layered = EditableMesh::FromTriangles(positions, uvs, normals, colors, { heights });
		const uint32_t top = std::bit_cast<uint32_t>(0.5f);
		const uint32_t bottom = std::bit_cast<uint32_t>(-0.5f);

		Driven driven("model_save_layers", nullptr, WritePrefab("model_save_layers", &layered));

		driven.editor.SetPick(ModelEditor::Pick::Faces);
		driven.Click(driven.Screen(glm::vec3(0.1f, 0.5f, 0.1f)));
		REQUIRE(driven.editor.Selected()->faces.size() == 1);

		driven.editor.Extrude();
		REQUIRE(driven.editor.Save());

		ModelEditor reopened;
		reopened.Open(driven.path);
		LoomTests::Pump();

		const EditableMesh* mesh = reopened.Editing();
		REQUIRE(mesh != nullptr);

		const std::vector<Loom::UVLayer> layers = mesh->ToUVLayers();
		REQUIRE(layers.size() == 1);
		CHECK(layers[0].id == LAYER);

		std::vector<float> saved{ };
		mesh->ToTriangles(saved, uvs, normals, colors);
		REQUIRE(layers[0].values.size() == saved.size() / POSITION_COMPONENTS);

		for (const uint32_t word : layers[0].values)
			CHECK((word == top || word == bottom));

		const auto tops = [top](const std::vector<uint32_t>& words) { return std::ranges::count(words, top); };
		CHECK(tops(layers[0].values) == tops(heights.values) + WALLS * CORNERS_PER_WALL);
	};

	TEST_CASE("The Model panel adds a shape, deletes what is picked, and steps back through both")
	{
		Driven driven("model_add");

		const size_t faces = driven.Mesh().faces.size();

		driven.editor.Add(Shapes::Shape::Cylinder);
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

		const EditableMesh plane = EditableMesh::Plane(1.0f);
		std::ofstream(driven.path, std::ios::binary) << ReadFile(WritePrefab("model_reload_plane", &plane));
		driven.Frame();

		CHECK(driven.Mesh().faces.size() == 1);

		driven.editor.Add(Shapes::Shape::Cube);
		std::ofstream(driven.path, std::ios::binary) << ReadFile(WritePrefab("model_reload_cube", &driven.cube));
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

		driven.editor.Add(Shapes::Shape::Cube);
		REQUIRE(driven.editor.Save());

		const std::string saved = ReadFile(driven.path);
		CHECK(saved.find("Component Mystery(6c1a4d55-0d3e-4b8e-9a31-6b2a1f0e7c42)") != std::string::npos);

		ModelEditor reopened;
		reopened.Open(driven.path);
		LoomTests::Pump();

		REQUIRE(reopened.Editing() != nullptr);
		CHECK(reopened.Editing()->faces.size() == 12);
	};

	TEST_CASE("The Model panel leaves animated meshes out")
	{
		LoomTests::EngineFixture engine;

		const EditableMesh cube = EditableMesh::Cube(1.0f);
		const std::string path = WritePrefab("model_skinned", nullptr, &cube, true);

		ModelEditor editor;
		editor.Open(path);
		LoomTests::Pump();

		CHECK(editor.Editing() == nullptr);

		std::error_code code;
		std::filesystem::remove(path, code);
	};

	TEST_CASE("The Model panel opens a skinned model on its joints, and the joint clicked is picked")
	{
		Driven driven("model_joints", nullptr, WriteJointedPrefab("model_joints"));

		Loom::GameObject* model = driven.editor.Model();
		REQUIRE(model != nullptr);

		CHECK(driven.editor.Editing() == nullptr);
		CHECK(!driven.renderer.clears.empty());
		CHECK(driven.editor.Joints() == std::vector<Loom::GameObject*>{ Named(*model, "Hinge"), Named(*model, "Tip") });
		CHECK(driven.editor.Selected()->joints.empty());

		driven.Click(driven.Screen(Placed(*Named(*model, "Tip"))));
		CHECK(driven.editor.Selected()->joints == std::set<uint32_t>{ 1 });

		driven.editor.SelectNone();
		driven.Click(driven.Screen(Placed(*Named(*model, "Hinge"))));
		CHECK(driven.editor.Selected()->joints == std::set<uint32_t>{ 0 });

		driven.editor.SetPick(ModelEditor::Pick::Faces);
		CHECK(driven.editor.Selected()->joints == std::set<uint32_t>{ 0 });
	};

	TEST_CASE("The Model panel's move handle lowers the picked joint and the skin with it, and undo puts it back")
	{
		const std::string path = WriteJointedPrefab("model_joint_move");
		const std::string written = ReadFile(path);

		Driven driven("model_joint_move", nullptr, path);

		Loom::GameObject* model = driven.editor.Model();
		Loom::GameObject* hinge = Named(*model, "Hinge");
		Loom::GameObject* tip = Named(*model, "Tip");
		Loom::GameObject* lid = Named(*model, "Lid");

		const glm::vec3 start = Placed(*tip);

		driven.Click(driven.Screen(start));
		REQUIRE(driven.editor.Selected()->joints == std::set<uint32_t>{ 1 });

		// Tip is near the top of the view, so it is dragged by its middle,
		// across the screen.
		constexpr float pull = 40.0f;

		const ImVec2 middle = driven.Screen(start);
		driven.Drag(middle, ImVec2(middle.x, middle.y + pull));

		const glm::vec3 moved = Placed(*tip);
		CHECK(moved.y < start.y - TOLERANCE);
		CHECK(Placed(*hinge) == glm::vec3(0.0f));

		// Every corner of the lid follows Tip down.
		Loom::Mesh* mesh = lid->GetComponent<Loom::Mesh>();
		lid->GetComponent<Loom::Skin>()->Deform(*mesh);
		const std::vector<float>& rest = *std::as_const(mesh->m_vertices);
		REQUIRE(mesh->m_deformed.size() == rest.size());

		for (size_t i = 1; i < rest.size(); i += 3)
			CHECK(std::abs(mesh->m_deformed[i] - rest[i] - (moved.y - start.y)) < TOLERANCE);

		driven.editor.Undo();
		CHECK(glm::distance(Placed(*tip), start) < TOLERANCE);

		driven.editor.Redo();
		CHECK(glm::distance(Placed(*tip), moved) < TOLERANCE);

		driven.editor.Undo();
		CHECK(driven.editor.Save());
		CHECK(ReadFile(path) == written);
	};

	TEST_CASE("The Model panel moves and turns a picked joint with those under it")
	{
		// How far handles are dragged, in screen pixels, and a quarter of the
		// way round the outer ring.
		constexpr float pull = 40.0f;
		constexpr float ring = 112.5f;

		Driven driven("model_joint_turn", nullptr, WriteJointedPrefab("model_joint_turn"));

		Loom::GameObject* model = driven.editor.Model();
		Loom::GameObject* hinge = Named(*model, "Hinge");
		Loom::GameObject* tip = Named(*model, "Tip");

		const glm::vec3 reach = Placed(*tip) - Placed(*hinge);

		driven.editor.SelectAll();
		REQUIRE(driven.editor.Selected()->joints == std::set<uint32_t>{ 0, 1 });

		const ImVec2 middle = driven.Screen((Placed(*hinge) + Placed(*tip)) / 2.0f);
		const ImVec2 aside = driven.Screen((Placed(*hinge) + Placed(*tip)) / 2.0f + glm::vec3(0.1f, 0.0f, 0.0f));
		const float across = std::hypot(aside.x - middle.x, aside.y - middle.y);
		const ImVec2 right((aside.x - middle.x) / across, (aside.y - middle.y) / across);

		constexpr float grab = 60.0f;

		driven.Drag(ImVec2(middle.x + right.x * grab, middle.y + right.y * grab), ImVec2(middle.x + right.x * (grab + pull), middle.y + right.y * (grab + pull)));

		CHECK((*hinge->transform.position).x > TOLERANCE);
		CHECK(*tip->transform.position == glm::vec3(0.0f, 1.0f, 0.0f));
		CHECK(glm::distance(Placed(*tip) - Placed(*hinge), reach) < TOLERANCE);

		driven.editor.SelectNone();
		driven.Click(driven.Screen(Placed(*hinge)));
		REQUIRE(driven.editor.Selected()->joints == std::set<uint32_t>{ 0 });

		const glm::vec3 before = Placed(*tip);
		const glm::vec3 hinged = *hinge->transform.position;
		const ImVec2 pivot = driven.Screen(Placed(*hinge));

		driven.editor.SetTool(ModelEditor::Tool::Rotate);
		driven.Drag(ImVec2(pivot.x + ring, pivot.y), ImVec2(pivot.x, pivot.y - ring));

		CHECK(std::abs(glm::distance(Placed(*tip), Placed(*hinge)) - glm::length(reach)) < TOLERANCE);
		CHECK(glm::distance(Placed(*tip), before) > 0.1f);
		CHECK(*tip->transform.position == glm::vec3(0.0f, 1.0f, 0.0f));

		// Turned about itself, so where it is and its size are as they were.
		CHECK(*hinge->transform.position == hinged);
		CHECK(*hinge->transform.scale == glm::vec3(1.0f));

		// Sized under the turned Hinge, Tip keeps its own turn.
		driven.editor.SelectNone();
		driven.Click(driven.Screen(Placed(*tip)));
		REQUIRE(driven.editor.Selected()->joints == std::set<uint32_t>{ 1 });

		const ImVec2 sized = driven.Screen(Placed(*tip));

		driven.editor.SetTool(ModelEditor::Tool::Scale);
		driven.Drag(sized, ImVec2(sized.x + pull, sized.y - pull));

		CHECK((*tip->transform.scale).x > 1.0f + TOLERANCE);
		CHECK(*tip->transform.rotation == glm::vec3(0.0f));
	};

	TEST_CASE("The Model panel saves the joints posed into the prefab, and only those")
	{
		const std::string path = WriteJointedPrefab("model_joint_save");

		Driven driven("model_joint_save", nullptr, path);

		Loom::GameObject* model = driven.editor.Model();
		Loom::GameObject* tip = Named(*model, "Tip");

		driven.Click(driven.Screen(Placed(*tip)));
		REQUIRE(driven.editor.Selected()->joints == std::set<uint32_t>{ 1 });

		constexpr float pull = 40.0f;

		const ImVec2 middle = driven.Screen(Placed(*tip));
		driven.Drag(middle, ImVec2(middle.x, middle.y + pull));
		REQUIRE((*tip->transform.position).y < 1.0f - TOLERANCE);

		CHECK(driven.editor.Save());

		Loom::Scene reader("Reader");
		LoomTests::Pump();

		Loom::GameObject* saved = Loom::SceneSerializer::Deserialize(ReadFile(path), reader.GetRoot());
		LoomTests::Pump();
		REQUIRE(saved != nullptr);

		CHECK(glm::distance(*Named(*saved, "Tip")->transform.position, *tip->transform.position) < TOLERANCE);
		CHECK(*Named(*saved, "Hinge")->transform.position == glm::vec3(0.0f));

		const std::string written = ReadFile(path);
		CHECK(driven.editor.Save());
		CHECK(ReadFile(path) == written);
	};

	TEST_CASE("The Model panel edits the Prefab panel's copy of the prefab it shows, and lets go of it when told")
	{
		const std::string path = WriteJointedPrefab("model_joint_live");
		const std::string unposed = ReadFile(path);

		Loom::Scene shown("Shown");
		LoomTests::Pump();

		Loom::GameObject* root = Loom::SceneSerializer::Deserialize(unposed, shown.GetRoot());
		LoomTests::Pump();
		REQUIRE(root != nullptr);

		Driven driven("model_joint_live", nullptr, path);
		REQUIRE(driven.editor.Model() != root);

		driven.editor.SetLive(path, &shown, root);
		driven.Frame();
		REQUIRE(driven.editor.Model() == root);

		Loom::GameObject* tip = Named(*root, "Tip");

		driven.Click(driven.Screen(Placed(*tip)));
		REQUIRE(driven.editor.Selected()->joints == std::set<uint32_t>{ 1 });

		constexpr float pull = 40.0f;

		const ImVec2 middle = driven.Screen(Placed(*tip));
		driven.Drag(middle, ImVec2(middle.x, middle.y + pull));
		CHECK((*tip->transform.position).y < 1.0f - TOLERANCE);
		CHECK(ReadFile(path) == unposed);
		CHECK(driven.editor.TakeEdited());
		CHECK_FALSE(driven.editor.TakeEdited());

		driven.editor.Undo();
		driven.Frame();
		CHECK(*tip->transform.position == glm::vec3(0.0f, 1.0f, 0.0f));
		CHECK(driven.editor.TakeEdited());

		// Found again when the Prefab panel changes its copy.
		Named(*root, "Lid")->GetComponent<Loom::Skin>()->m_joints = "Hinge\n";
		driven.Frame();
		CHECK(driven.editor.Model() == root);
		CHECK(driven.editor.Joints() == std::vector<Loom::GameObject*>{ Named(*root, "Hinge") });

		driven.editor.SetLive("", nullptr, nullptr);
		driven.Frame();
		CHECK(driven.editor.Model() != nullptr);
		CHECK(driven.editor.Model() != root);
		CHECK(ReadFile(path) == unposed);
	};

	// The mesh's new material reads the project's shader, which the web build
	// fetches, and node cannot.
#ifndef __EMSCRIPTEN__
	TEST_CASE("The Model panel gives a prefab with no mesh one, drawn with a shader its project has")
	{
		const std::filesystem::path project = std::filesystem::temp_directory_path() / "model_add_mesh";
		const std::filesystem::path prefab = project / "Assets" / "Prefabs" / "Empty.loomprefab";

		std::filesystem::create_directories(prefab.parent_path());
		std::ofstream(project / "Model.loomproject") << "name=Model\n";
		std::ofstream(project / "Assets" / "Lit.shader") << "";
		std::ofstream(prefab, std::ios::binary) << "GameObject Empty(5d0c6b7e-2f43-4a8e-9c1d-7a6e0b3f2c11)\n\t0 = 0\n\t1 = false\n\t2 = (0, 0, 0)\n\t3 = (0, 0, 0)\n\t4 = (1, 1, 1)\n\t5 = false\n";

		{
			// The shader is empty, so it does not compile.
			const LoomTests::Captured captured(std::cerr);
			Driven driven("model_add_mesh", nullptr, prefab.string());
			CHECK(driven.editor.Editing() == nullptr);

			driven.editor.AddMesh();
			CHECK(driven.Mesh().faces.size() == 6);
			REQUIRE(driven.editor.Save());

			ModelEditor reopened;
			reopened.Open(prefab.string());
			LoomTests::Pump();

			REQUIRE(reopened.Editing() != nullptr);
			CHECK(reopened.Editing()->faces.size() == 6);
			CHECK(ReadFile(prefab.string()).find("\"Assets/Lit.shader\"") != std::string::npos);
			CHECK(captured.Says("Lit.shader"));
		};

		std::error_code code;
		std::filesystem::remove_all(project, code);
	};

	TEST_CASE("The Model panel lists its project's shaders and saves the one chosen into the prefab, keeping its guids")
	{
		const std::filesystem::path project = std::filesystem::temp_directory_path() / "model_shaders";
		const std::filesystem::path prefab = project / "Assets" / "Prefabs" / "Crate.loomprefab";
		const EditableMesh cube = EditableMesh::Cube(1.0f);

		std::filesystem::create_directories(prefab.parent_path());
		std::ofstream(project / "Model.loomproject") << "name=Model\n";
		std::ofstream(project / "Assets" / "Lit.shader") << "";
		std::ofstream(project / "Assets" / "Model.shader") << "";
		std::ofstream(prefab, std::ios::binary) << ReadFile(WritePrefab("model_shaders_source", &cube, &cube));

		{
			// The shaders are empty, so they do not compile.
			const LoomTests::Captured captured(std::cerr);
			Driven driven("model_shaders", nullptr, prefab.string());
			CHECK(driven.editor.Shaders() == std::vector<std::string>{ "Assets/Lit.shader", "Assets/Model.shader" });

			Loom::Prefabs::ObjectNode before;
			REQUIRE(Loom::Prefabs::Parse(ReadFile(prefab.string()), before));

			driven.editor.SetShader("Assets/Model.shader");
			REQUIRE(driven.editor.Save());

			const std::string saved = ReadFile(prefab.string());
			const auto named = [&](size_t from) { return saved.find("\"Assets/Model.shader\"", from); };
			REQUIRE(named(0) != std::string::npos);
			CHECK(named(named(0) + 1) != std::string::npos);

			Loom::Prefabs::ObjectNode after;
			REQUIRE(Loom::Prefabs::Parse(saved, after));
			CHECK(after.guid == before.guid);
			REQUIRE(after.components.size() == before.components.size());

			for (size_t i = 0; i < after.components.size(); i++)
				CHECK(after.components[i].guid == before.components[i].guid);

			CHECK(captured.Says("Material: Assets/Model.shader: "));
		};

		std::error_code code;
		std::filesystem::remove_all(project, code);
		std::filesystem::remove(std::filesystem::temp_directory_path() / "model_shaders_source.loomprefab", code);
	};
#endif

	TEST_CASE("The Model panel's Esc puts a dragged face back and keeps what redo would do")
	{
		Driven driven("model_escape");

		driven.editor.SetPick(ModelEditor::Pick::Faces);
		driven.Click(driven.Screen(glm::vec3(0.1f, 0.5f, 0.1f)));
		REQUIRE(driven.editor.Selected()->faces.size() == 1);

		const uint32_t face = *driven.editor.Selected()->faces.begin();
		const glm::vec3 centre = driven.Mesh().FaceCentre(face);

		driven.editor.Flip();
		driven.editor.Undo();

		const ImVec2 middle = driven.Screen(centre);
		const ImVec2 above = driven.Screen(centre + glm::vec3(0.0f, 0.1f, 0.0f));
		const float reach = std::hypot(above.x - middle.x, above.y - middle.y);
		const ImVec2 up((above.x - middle.x) / reach, (above.y - middle.y) / reach);

		constexpr float grab = 60.0f;
		constexpr float pull = 40.0f;

		ImGuiIO& io = ImGui::GetIO();
		io.AddMousePosEvent(middle.x + up.x * grab, middle.y + up.y * grab);
		driven.Frame();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		driven.Frame();
		io.AddMousePosEvent(middle.x + up.x * (grab + pull), middle.y + up.y * (grab + pull));
		driven.Frame();

		REQUIRE(driven.Mesh().FaceCentre(face).y > centre.y + TOLERANCE);

		driven.Key(ImGuiKey_Escape);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		driven.Frame();

		CHECK(std::abs(driven.Mesh().FaceCentre(face).y - centre.y) < TOLERANCE);

		driven.editor.Redo();
		CHECK(driven.Mesh().FaceNormal(face).y < -1.0f + TOLERANCE);
	};

	TEST_CASE("The Model panel's extrude of faces facing every way does nothing, and is no step")
	{
		Driven driven("model_extrude_all");

		driven.editor.Add(Shapes::Shape::Cube);
		REQUIRE(driven.Mesh().faces.size() == 12);

		driven.editor.SetPick(ModelEditor::Pick::Faces);
		driven.editor.SelectAll();
		driven.editor.Extrude();
		CHECK(driven.Mesh().faces.size() == 12);

		driven.editor.Undo();
		CHECK(driven.Mesh().faces.size() == 6);
	};

	TEST_CASE("The Model panel shows a prefab deleted from under it as one it cannot read")
	{
		Driven driven("model_deleted");
		const LoomTests::Captured captured(std::cerr);

		std::error_code code;
		std::filesystem::remove(driven.path, code);

		driven.Frame();
		driven.Frame();

		CHECK(driven.editor.Editing() == nullptr);

		driven.editor.AddMesh();
		driven.Frame();

		CHECK(driven.editor.Editing() == nullptr);
		CHECK(captured.Says(driven.path));
	};

	TEST_CASE("The Model panel's ? opens its controls in a popup")
	{
		Driven driven("model_controls");

		REQUIRE_FALSE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));

		driven.Run("click \"Model\" \"?\"\nwait 2\n");

		CHECK(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
	};
};
