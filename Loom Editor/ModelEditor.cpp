#include "ModelEditor.h"

#include "EditorIcons.h"
#include "Prefabs.h"
#include "ProjectAssets.h"
#include "ProjectTemplate.h"

#include "Engine.h"
#include "GameObject.h"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "OpenGL.h"
#include "Renderer.h"
#include "RenderMath.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "SerializedField.h"
#include "Skin.h"

#include "glm/gtc/constants.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <tuple>
#include <unordered_map>
#include <utility>


namespace Loom
{
	namespace
	{
		// A window docked on the frame it first appears only takes its tab on
		// the one after.
		constexpr int focus_frames = 2;

		constexpr size_t floats_per_position = 3;

		constexpr const char* model_light = "Model Light";

		constexpr size_t max_steps = 100;
		constexpr double message_seconds = 4.0;

		constexpr float side_width = 230.0f;

		// The camera starts above and to one side, looking down at the model.
		constexpr float start_yaw = 35.0f;
		constexpr float start_pitch = -25.0f;
		constexpr float max_pitch = 89.0f;
		constexpr float orbit_degrees_per_pixel = 0.4f;
		constexpr float zoom_per_notch = 0.85f;

		// How much farther than the model's bounds just fill the view it stands.
		constexpr float fit_margin = 1.3f;
		constexpr float empty_distance = 5.0f;
		constexpr float min_distance = 0.01f;

		// The near and far planes, as shares of the distance to the target.
		constexpr float near_share = 0.01f;
		constexpr float far_share = 100.0f;

		// Screen pixels.
		constexpr float pick_radius = 10.0f;
		constexpr float drag_threshold = 4.0f;
		constexpr float dot_half_size = 3.5f;
		constexpr float min_dot_half_size = 1.0f;
		constexpr float joint_half_size = 5.0f;

		// A vertex dot's half size is this share of the mesh's mean edge as
		// drawn there, down to min_dot_half_size, so dots shrink apart as the
		// view backs off.
		constexpr float dot_edge_share = 0.25f;

		// How near a joint's position, size or turn in degrees, back out of
		// its matrix, is taken to be the one it had, so changing one of them
		// writes no rounding into the others.
		constexpr float pose_tolerance = 1e-5f;
		constexpr float turn_tolerance = 1e-3f;
		constexpr float wire_half_width = 0.6f;
		constexpr float picked_wire_half_width = 1.4f;
		constexpr float handle_length = 90.0f;
		constexpr float handle_hit = 8.0f;
		constexpr float handle_width = 3.0f;
		constexpr float arrow_size = 10.0f;
		constexpr float centre_size = 7.0f;
		constexpr float view_ring_share = 1.25f;
		constexpr int ring_points = 64;

		// Toward the eye, as a share of the way there, so the wire and dots
		// draw over the faces they lie on.
		constexpr float overlay_pull = 0.002f;

		// A point counts as seen when nothing is in front of it by more than
		// this share of the way from the eye.
		constexpr float seen_share = 1e-3f;

		// An extrusion lifts faces this share of their edges' average length,
		// and a shape added is this share of the mesh's size.
		constexpr float extrude_share = 0.5f;
		constexpr float add_share = 0.5f;
		constexpr float empty_size = 1.0f;

		// With Ctrl held, handles move, turn and size in these steps.
		constexpr float move_snap = 0.125f;
		constexpr float turn_snap = 15.0f;
		constexpr float scale_snap = 0.1f;

		// Short of nothing, where what is picked would fold flat.
		constexpr float min_scale = 0.01f;

		constexpr float centre_drag_speed = 0.01f;

		constexpr int axes = 3;
		const glm::vec3 axis_vectors[axes] = { X_AXIS, Y_AXIS, Z_AXIS };
		const char* const axis_names[axes] = { "X", "Y", "Z" };

		constexpr ImU32 free_colour = IM_COL32(235, 235, 235, 255);
		constexpr ImU32 lit_colour = IM_COL32(255, 220, 60, 255);
		constexpr ImU32 box_fill = IM_COL32(255, 160, 40, 40);
		constexpr ImU32 box_border = IM_COL32(255, 160, 40, 200);

		const glm::vec4 wire_colour(0.08f, 0.08f, 0.08f, 0.85f);
		const glm::vec4 picked_colour(1.0f, 0.58f, 0.12f, 1.0f);
		const glm::vec4 hovered_colour(1.0f, 1.0f, 1.0f, 1.0f);
		const glm::vec4 picked_fill(1.0f, 0.58f, 0.12f, 0.3f);
		const glm::vec4 hovered_fill(1.0f, 1.0f, 1.0f, 0.15f);
		const glm::vec4 bone_colour(0.85f, 0.85f, 0.85f, 0.9f);

		// How much of the background is laid over the model while its joints
		// are picked, so they stand out in front of it.
		constexpr float fade_alpha = 0.65f;

		// The whole view, as two triangles in the device's own space.
		const glm::vec3 screen_corners[] =
		{
			{ -1.0f, -1.0f, 0.0f }, { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f },
			{ -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }, { -1.0f, 1.0f, 0.0f },
		};

		// The ground's lines either side of the middle one.
		constexpr int grid_half_lines = 12;
		constexpr float grid_share = 0.25f;
		const glm::vec4 grid_colour(1.0f, 1.0f, 1.0f, 0.12f);
		constexpr float grid_axis_alpha = 0.6f;

		const glm::vec3 axis_colours[axes] = { { 0.9f, 0.27f, 0.27f }, { 0.43f, 0.78f, 0.27f }, { 0.27f, 0.51f, 0.94f } };

		const char* const overlay_vertex = R"(
layout(location = 0) in vec3 aPos;

uniform mat4 u_viewProjection;
uniform vec3 u_cameraPosition;
uniform float u_pull;

void main()
{
	gl_Position = u_viewProjection * vec4(mix(aPos, u_cameraPosition, u_pull), 1.0);
}
)";

		const char* const overlay_fragment = R"(
uniform vec4 u_color;

out vec4 FragColor;

void main()
{
	FragColor = u_color;
}
)";

		struct PickInfo
		{
			ModelEditor::Pick pick;
			const char* label;
			const char* tip;
			ImGuiKey key;
		};

		constexpr PickInfo picks[] =
		{
			{ ModelEditor::Pick::Vertices, "Vertices", "Click picks corners", ImGuiKey_1 },
			{ ModelEditor::Pick::Edges, "Edges", "Click picks edges", ImGuiKey_2 },
			{ ModelEditor::Pick::Faces, "Faces", "Click picks faces", ImGuiKey_3 },
			{ ModelEditor::Pick::Joints, "Joints", "Click picks the joints a skin bends by", ImGuiKey_4 },
		};

		struct ToolInfo
		{
			ModelEditor::Tool tool;
			const char* label;
			const char* tip;
			ImGuiKey key;
		};

		constexpr ToolInfo tools[] =
		{
			{ ModelEditor::Tool::Move, "Move", "The handles move what is picked", ImGuiKey_W },
			{ ModelEditor::Tool::Rotate, "Rotate", "The rings turn what is picked", ImGuiKey_E },
			{ ModelEditor::Tool::Scale, "Scale", "The handles size what is picked", ImGuiKey_R },
		};

		const char* const controls =
			"Left click: pick what is under the mouse\n"
			"Shift + click: add to or take from what is picked\n"
			"Drag a box: pick everything in it\n"
			"Drag a handle: move, turn or size what is picked\n"
			"Hold Ctrl while dragging a handle: snap\n"
			"Right drag or Alt + left drag: orbit\n"
			"Middle drag: pan\n"
			"Wheel: zoom\n"
			"F: frame what is picked, or the whole model\n"
			"1, 2, 3, 4: pick vertices, edges, faces, joints\n"
			"W, E, R: move, rotate, scale\n"
			"Ctrl+A: pick everything, Esc: pick nothing";

		const char* PickName(ModelEditor::Pick pick, size_t count)
		{
			const bool one = count == 1;

			switch (pick)
			{
			case ModelEditor::Pick::Vertices:	return one ? "vertex" : "vertices";
			case ModelEditor::Pick::Edges:		return one ? "edge" : "edges";
			case ModelEditor::Pick::Joints:		return one ? "joint" : "joints";
			default:							return one ? "face" : "faces";
			};
		};

		float Distance(const ImVec2& a, const ImVec2& b)
		{
			return std::hypot(a.x - b.x, a.y - b.y);
		};

		float SegmentDistance(const ImVec2& point, const ImVec2& a, const ImVec2& b)
		{
			const ImVec2 ab(b.x - a.x, b.y - a.y);
			const float length = ab.x * ab.x + ab.y * ab.y;
			const float along = length > 0.0f ? std::clamp(((point.x - a.x) * ab.x + (point.y - a.y) * ab.y) / length, 0.0f, 1.0f) : 0.0f;

			return Distance(point, ImVec2(a.x + ab.x * along, a.y + ab.y * along));
		};

		float Snap(float value, float step)
		{
			return std::round(value / step) * step;
		};

		// Two directions square to the axis and to each other, to draw its ring with.
		void Around(const glm::vec3& axis, glm::vec3& u, glm::vec3& v)
		{
			u = glm::normalize(glm::cross(axis, std::abs(axis.y) < std::abs(axis.x) ? Y_AXIS : X_AXIS));
			v = glm::cross(axis, u);
		};

		std::filesystem::file_time_type WriteTime(const std::string& path)
		{
			std::error_code code;
			return std::filesystem::last_write_time(path, code);
		};

		// Absolute, since opening a project moves the working directory.
		std::string Absolute(const std::string& path)
		{
			std::error_code code;
			return std::filesystem::absolute(path, code).lexically_normal().string();
		};

		// The prefab's file, or until there is one, the text its tab was opened
		// with, which is dropped once the file is found.
		std::string PrefabText(const std::string& path, std::string& unwritten)
		{
			std::error_code code;

			if (!std::filesystem::exists(path, code))
				return unwritten;

			unwritten.clear();

			return ProjectAssets::ReadText(path);
		};

		void MergeKeys(const std::vector<Prefabs::Field>& from, std::vector<Prefabs::Field>& into, const std::vector<std::string>& keys)
		{
			for (const std::string& key : keys)
			{
				const auto value = std::find_if(from.begin(), from.end(), [&](const Prefabs::Field& field) { return field.key == key; });

				if (value == from.end())
					continue;

				const auto found = std::find_if(into.begin(), into.end(), [&](const Prefabs::Field& field) { return field.key == key; });

				if (found == into.end())
					into.push_back(*value);
				else found->value = value->value;
			};
		};

		// Writes the fields edited here, the meshes' geometry, the materials'
		// shaders and the joints' transforms, into the file's own tree rather
		// than this copy over it, so the file keeps its guids, and any
		// component this build could not read stays as it was. Components pair
		// up as the nth of their type on each side, and one the file's object
		// lacks, such as a mesh added here, is added.
		void MergeFields(const Prefabs::ObjectNode& shown, Prefabs::ObjectNode& file, const std::map<std::string, std::vector<std::string>>& edited)
		{
			if (const auto keys = edited.find(shown.guid); keys != edited.end())
				MergeKeys(shown.fields, file.fields, keys->second);

			std::map<std::string, size_t> seen{ };

			for (const Prefabs::ComponentNode& component : shown.components)
			{
				size_t before = seen[component.type]++;

				const auto counterpart = std::find_if(file.components.begin(), file.components.end(),
					[&](const Prefabs::ComponentNode& other) { return other.type == component.type && before-- == 0; });

				if (counterpart == file.components.end())
				{
					file.components.push_back(component);
					continue;
				};

				if (const auto keys = edited.find(component.guid); keys != edited.end())
					MergeKeys(component.fields, counterpart->fields, keys->second);
			};

			// Objects are always read, so children pair up by place.
			for (size_t i = 0; i < shown.children.size() && i < file.children.size(); i++)
				MergeFields(shown.children[i], file.children[i], edited);
		};

		void FindParts(GameObject& object, std::vector<GameObject*>& found, std::vector<GameObject*>& fixed)
		{
			if (const Mesh* mesh = object.GetComponent<Mesh>())
			{
				if (!mesh->m_generated && *std::as_const(mesh->primitive_id) == Mesh::Triangles)
					found.push_back(&object);
				else fixed.push_back(&object);
			};

			for (GameObject* child : object.GetChildren())
				FindParts(*child, found, fixed);
		};

		void SkinsOf(GameObject& object, std::vector<Skin*>& found)
		{
			if (Skin* skin = object.GetComponent<Skin>())
				found.push_back(skin);

			for (GameObject* child : object.GetChildren())
				SkinsOf(*child, found);
		};

		// The joints under object, in the order a walk meets them, each with
		// how many of them are above it.
		void JointsUnder(GameObject& object, const std::set<GameObject*>& joints, int depth, std::vector<std::pair<GameObject*, int>>& found)
		{
			const bool joint = joints.contains(&object);

			if (joint)
				found.push_back({ &object, depth });

			for (GameObject* child : object.GetChildren())
				JointsUnder(*child, joints, joint ? depth + 1 : depth, found);
		};

		// The meshes under root that can be shaped, those that cannot, and the
		// joints its skins bend by, found by name under each skin's skeleton
		// as the skin finds them.
		void ContentsOf(GameObject& root, std::vector<GameObject*>& parts, std::vector<GameObject*>& fixed, std::vector<std::pair<GameObject*, int>>& joints)
		{
			FindParts(root, parts, fixed);

			std::vector<Skin*> skins{ };
			SkinsOf(root, skins);

			std::set<GameObject*> named{ };

			for (Skin* skin : skins)
				if (GameObject* skeleton = *std::as_const(skin->skeleton))
				{
					const std::unordered_map<std::string, GameObject*> under = DescendantsByName(*skeleton);

					for (const std::string& name : SplitLines(*std::as_const(skin->m_joints)))
						if (const auto joint = under.find(name); joint != under.end())
							named.insert(joint->second);
				};

			JointsUnder(root, named, 0, joints);
		};

		std::vector<GameObject*> MadeOf(GameObject& root)
		{
			std::vector<GameObject*> made{ };
			std::vector<GameObject*> fixed{ };
			std::vector<std::pair<GameObject*, int>> joints{ };
			ContentsOf(root, made, fixed, joints);

			made.insert(made.end(), fixed.begin(), fixed.end());

			for (const auto& [joint, depth] : joints)
				made.push_back(joint);

			return made;
		};

		glm::vec3 Placed(const GameObject& object)
		{
			return glm::vec3(object.WorldMatrix()[3]);
		};

		void MaterialsOf(GameObject& object, std::vector<Material*>& found)
		{
			if (Material* material = object.GetComponent<Material>())
				found.push_back(material);

			for (GameObject* child : object.GetChildren())
				MaterialsOf(*child, found);
		};

		// The folder holding the project the prefab is in, empty for none.
		std::filesystem::path ProjectFolder(const std::string& prefab)
		{
			std::error_code code;

			for (std::filesystem::path folder = std::filesystem::path(prefab).parent_path(); folder.has_relative_path(); folder = folder.parent_path())
			{
				const bool project = std::any_of(
					std::filesystem::directory_iterator(folder, code),
					std::filesystem::directory_iterator(),
					[](const std::filesystem::directory_entry& entry) { return entry.path().extension() == ProjectTemplate::extension; });

				if (project)
					return folder;
			};

			return { };
		};

		// Every shader the project of the prefab has, relative to the project, in order.
		std::vector<std::string> ProjectShaders(const std::string& prefab)
		{
			return ProjectAssets::ShadersIn(ProjectFolder(prefab).string());
		};

		// The shader another mesh of the prefab draws with, so a mesh added to
		// it looks like the rest.
		std::string ShaderOf(GameObject& object)
		{
			if (Material* material = object.GetComponent<Material>())
				return material->GetShaderPath();

			for (GameObject* child : object.GetChildren())
				if (std::string found = ShaderOf(*child); !found.empty())
					return found;

			return "";
		};

	};

	ModelEditor::~ModelEditor()
	{
		for (const std::unique_ptr<Document>& document : m_documents)
			Unload(*document);

		Renderer* renderer = Renderer::Get();

		if (renderer && m_program)
			renderer->DeleteProgram(m_program);
	};

	bool ModelEditor::Compile()
	{
		if (m_compileTried)
			return m_program != 0;

		m_compileTried = true;

		try
		{
			m_program = Renderer::Get()->CreateProgram("Loom Editor Model Overlay", overlay_vertex, overlay_fragment);
		}
		catch (const std::exception& e)
		{
			std::cerr << "Model panel: " << e.what() << std::endl;
		};

		return m_program != 0;
	};

	void ModelEditor::Open(const std::string& path)
	{
		Open(path, "");
	};

	void ModelEditor::Open(const std::string& path, const std::string& text)
	{
		const std::string normal = Absolute(path);

		m_focusFrames = focus_frames;

		for (const std::unique_ptr<Document>& document : m_documents)
			if (document->path == normal)
			{
				document->select = true;
				return;
			};

		std::unique_ptr<Document> document = std::make_unique<Document>();
		document->path = normal;
		document->unwritten = text;

		if (!Load(*document))
			return;

		m_documents.push_back(std::move(document));
	};

	bool ModelEditor::IsOpen(const std::string& path) const
	{
		const std::string normal = Absolute(path);

		return std::ranges::any_of(m_documents,
			[&normal](const std::unique_ptr<Document>& document) { return document->path == normal; });
	};

	bool ModelEditor::Load(Document& document)
	{
		document.stale = false;

		if (document.path == m_live)
		{
			document.scene = m_liveScene;
			document.root = m_liveRoot;
			document.borrowed = true;
		}
		else
		{
			const std::string text = PrefabText(document.path, document.unwritten);
			Prefabs::ObjectNode prefab;
			std::string error;

			if (!Prefabs::Parse(text, prefab, &error))
			{
				std::cerr << "Model panel: could not read " << document.path << ": " << error << std::endl;
				return false;
			};

			Scene* scene = new Scene(std::filesystem::path(document.path).stem().string());
			Scene::Unlist(scene);

			GameObject* root = SceneSerializer::Deserialize(Prefabs::Write(prefab), scene->GetRoot(), &error);

			if (root == nullptr)
			{
				std::cerr << "Model panel: could not open " << document.path << ": " << error << std::endl;
				Engine::QueueTask([scene]() { delete scene; });
				return false;
			};

			scene->AddChild(model_light)->Attach<Light>();

			document.scene = scene;
			document.root = root;
			document.written = WriteTime(document.path);
			document.text = text;
		};

		// Children join their parents through the queue, so the meshes under
		// the top object are only found once it has run.
		Engine::DoTasks();

		std::vector<GameObject*> found{ };
		std::vector<std::pair<GameObject*, int>> joints{ };
		document.fixed.clear();
		ContentsOf(*document.root, found, document.fixed, joints);
		document.made = MadeOf(*document.root);

		for (GameObject* object : found)
		{
			Mesh* mesh = object->GetComponent<Mesh>();
			Skin* skin = object->GetComponent<Skin>();
			const std::vector<float>& positions = *std::as_const(mesh->m_vertices);
			const std::vector<float> bindings = skin ? skin->Bindings(positions.size() / floats_per_position) : std::vector<float>{ };

			EditableMesh editing = EditableMesh::FromTriangles(positions, *std::as_const(mesh->m_textureCoordinates), *std::as_const(mesh->m_normals), *std::as_const(mesh->m_colors), mesh->UVLayers(), bindings, skin ? skin->BindingFloats() : 0);
			editing.JoinTriangles();

			document.parts.push_back(Part{ object, mesh, std::move(editing), skin });
			Bend(document.parts.back());
		};

		document.joints.clear();

		for (const auto& [joint, depth] : joints)
			document.joints.push_back(Joint{ joint, depth });

		const std::vector<JointPose> poses = Poses(document);

		for (size_t i = 0; i < poses.size(); i++)
			document.joints[i].saved = poses[i];

		// A model with nothing to shape opens on its joints.
		if (document.joints.empty() && document.pick == Pick::Joints)
			document.pick = Pick::Faces;
		else if (!document.joints.empty() && document.parts.empty())
			document.pick = Pick::Joints;

		if (document.distance == 0.0f)
		{
			document.yaw = start_yaw;
			document.pitch = start_pitch;
			Frame(document);
		};

		return true;
	};

	void ModelEditor::Unload(Document& document)
	{
		// Deferred, as a closed scene is, to the next time the queue runs. The
		// Prefab panel's copy is that panel's to delete.
		if (Scene* scene = std::exchange(document.scene, nullptr); scene && !document.borrowed)
			Engine::QueueTask([scene]() { delete scene; });

		document.borrowed = false;
		document.made.clear();
		document.root = nullptr;
		document.parts.clear();
		document.fixed.clear();
		document.joints.clear();
		document.part = 0;
		document.selection = { };
		document.undo.clear();
		document.redo.clear();
		document.drag = Drag::None;
		document.hoveredHandle.reset();
		document.hoveredVertex.reset();
		document.hoveredEdge.reset();
		document.hoveredFace.reset();
		document.hoveredJoint.reset();
		document.pickedOf.reset();
	};

	void ModelEditor::Relocate(const std::string& from, const std::string& to)
	{
		for (const std::unique_ptr<Document>& document : m_documents)
			document->path = ProjectAssets::Relocate(document->path, from, to);
	};

	std::vector<std::string> ModelEditor::TakeSaved()
	{
		return std::exchange(m_saved, { });
	};

	void ModelEditor::SetLive(const std::string& path, Scene* scene, GameObject* root)
	{
		m_live = root ? Absolute(path) : "";
		m_liveScene = scene;
		m_liveRoot = root;

		for (const std::unique_ptr<Document>& document : m_documents)
			if (document->borrowed || document->path == m_live)
			{
				if (document->unsaved)
					SaveDocument(*document);

				Unload(*document);
				document->stale = true;
			};
	};

	bool ModelEditor::TakeEdited()
	{
		return std::exchange(m_edited, false);
	};

	bool ModelEditor::SaveAll()
	{
		bool saved = true;

		for (const std::unique_ptr<Document>& document : m_documents)
			if (document->unsaved)
				saved = SaveDocument(*document) && saved;

		return saved;
	};

	bool ModelEditor::SaveDocument(Document& document)
	{
		// The Prefab panel writes its copy as it saves its own changes.
		if (document.borrowed)
		{
			m_edited = std::exchange(document.unsaved, false) || m_edited;
			return true;
		};

		Prefabs::ObjectNode file;
		Prefabs::ObjectNode shown;
		std::string error;

		if (document.root == nullptr ||
			!Prefabs::Parse(PrefabText(document.path, document.unwritten), file, &error) ||
			!Prefabs::Parse(SceneSerializer::Serialize(*document.root), shown, &error))
		{
			std::cerr << "Model panel: could not save " << document.path << ": " << error << std::endl;
			return false;
		};

		// Each mesh's geometry, and its skin's bindings, under the keys of their
		// fields that are not references, which point at this copy's objects
		// rather than the file's, and each material's shader.
		std::map<std::string, std::vector<std::string>> edited{ };

		const auto values = [&edited](const LoomObject& object)
		{
			std::vector<std::string>& keys = edited[object.GetGuid().ToString()];
			const std::vector<SerializedField>& fields = object.GetFields();

			for (size_t i = 0; i < fields.size(); i++)
				if (fields[i].get_reference == nullptr && object.IsSaved(fields[i]))
					keys.push_back(std::to_string(i));
		};

		for (const Part& part : document.parts)
		{
			values(*part.mesh);

			if (part.skin)
				values(*part.skin);
		};

		std::vector<Material*> materials{ };
		MaterialsOf(*document.root, materials);

		for (Material* material : materials)
		{
			const std::vector<SerializedField>& fields = material->GetFields();

			for (size_t i = 0; i < fields.size(); i++)
				if (fields[i].data == &material->GetShaderPath())
					edited[material->GetGuid().ToString()].push_back(std::to_string(i));
		};

		// Only the joints posed here, so the rest keep their values as written.
		const std::vector<JointPose> poses = Poses(document);

		for (size_t i = 0; i < poses.size(); i++)
			if (poses[i] != document.joints[i].saved)
				edited[document.joints[i].object->GetGuid().ToString()] = Prefabs::TransformKeys(*document.joints[i].object);

		MergeFields(shown, file, edited);

		const std::string text = Prefabs::Write(file);

		{
			std::ofstream out(document.path, std::ios::binary);
			out << text;

			if (!out)
			{
				std::cerr << "Model panel: could not write " << document.path << std::endl;
				return false;
			};
		};

		document.written = WriteTime(document.path);
		document.text = text;
		document.unwritten.clear();
		document.unsaved = false;
		document.message = "Saved.";
		document.messageTime = ImGui::GetTime();

		for (size_t i = 0; i < poses.size(); i++)
			document.joints[i].saved = poses[i];

		m_saved.push_back(document.path);

		return true;
	};

	ModelEditor::Document* ModelEditor::Front() const
	{
		if (m_front)
			return m_front;

		return m_documents.empty() ? nullptr : m_documents.back().get();
	};

	const EditableMesh* ModelEditor::Editing() const
	{
		const Document* document = Front();

		return document && !document->parts.empty() ? &document->parts[document->part].editing : nullptr;
	};

	const ModelEditor::Selection* ModelEditor::Selected() const
	{
		const Document* document = Front();

		return document ? &document->selection : nullptr;
	};

	std::optional<ImVec2> ModelEditor::ScreenOf(const glm::vec3& point) const
	{
		const Document* document = Front();

		if (document == nullptr)
			return std::nullopt;

		return Project(document->view, glm::vec3(document->view.world * glm::vec4(point, 1.0f)));
	};

	void ModelEditor::Draw(bool* open)
	{
		if (m_focusFrames > 0)
		{
			m_focusFrames--;
			ImGui::SetNextWindowFocus();
		};

		m_focused = false;
		m_front = nullptr;

		// Read again when changed elsewhere, unless there are edits here to
		// keep. The Prefab panel's copy is checked for objects gone or added,
		// whose deletion may run before that panel says it changed.
		for (const std::unique_ptr<Document>& document : m_documents)
		{
			if (document->borrowed && document->drag == Drag::None && MadeOf(*document->root) != document->made)
				document->stale = true;

			if (document->stale)
			{
				Unload(*document);
				Load(*document);
				continue;
			};

			if (document->borrowed || document->unsaved || document->drag != Drag::None || WriteTime(document->path) == document->written)
				continue;

			document->written = WriteTime(document->path);

			if (ProjectAssets::ReadText(document->path) == document->text)
				continue;

			Unload(*document);
			Load(*document);
		};

		// The wheel zooms the view rather than scrolling the panel.
		if (ImGui::Begin(EditorPanels::Model, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

			if (m_documents.empty())
				ImGui::TextDisabled("Right-click a prefab or model in the Project panel and pick Edit Model to shape it here.");
			else if (ImGui::BeginTabBar("##models", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll))
			{
				const Document* closed = nullptr;

				for (const std::unique_ptr<Document>& document : m_documents)
				{
					ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;

					if (document->unsaved)
						flags |= ImGuiTabItemFlags_UnsavedDocument;

					if (std::exchange(document->select, false))
						flags |= ImGuiTabItemFlags_SetSelected;

					// Known by the document rather than its path, so a prefab
					// moved in the Project panel keeps its tab.
					const std::string label = std::string(EditorIcons::Model) + " " + std::filesystem::path(document->path).stem().string() + "###" + std::to_string((uintptr_t)document.get());
					bool keep = true;
					const bool shown = ImGui::BeginTabItem(label.c_str(), &keep, flags);

					ImGui::SetItemTooltip("%s", document->path.c_str());

					if (shown)
					{
						m_front = document.get();
						DrawDocument(*document);
						ImGui::EndTabItem();
					};

					if (!keep)
					{
						if (document->unsaved)
							m_closing = document.get();
						else
							closed = document.get();
					};
				};

				ImGui::EndTabBar();
				Close(closed);
			};

			DrawCloseQuestion();
		};

		ImGui::End();

		for (const std::unique_ptr<Document>& document : m_documents)
			if (document->borrowed && document->unsaved)
				SaveDocument(*document);
	};

	void ModelEditor::DrawDocument(Document& document)
	{
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S))
			SaveDocument(document);

		// Asked every frame, mid-drag too: ImGui only routes a shortcut to a
		// window that asked for it the frame before.
		const bool undo = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z);
		const bool redo = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);

		if (document.drag == Drag::None && (undo || redo))
			Step(document, undo);

		HandleKeys(document);
		DrawToolbar(document);

		const float status_height = ImGui::GetTextLineHeightWithSpacing();
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float height = available.y - status_height;

		DrawSide(document);
		ImGui::SameLine();

		if (document.parts.empty() && document.fixed.empty())
		{
			ImGui::BeginGroup();

			if (document.root == nullptr)
				ImGui::TextDisabled("This prefab could not be read. It may have been moved, deleted or broken.");
			else
			{
				ImGui::TextDisabled("This prefab has no mesh to shape yet.");

				if (ImGui::Button("Add a Mesh"))
					AddMesh();

				ImGui::SetItemTooltip("Gives the prefab's top object a mesh, starting as a cube.");
			};

			ImGui::EndGroup();
		}
		else DrawCanvas(document, ImVec2(available.x - side_width - ImGui::GetStyle().ItemSpacing.x, height));

		DrawStatus(document);
	};

	void ModelEditor::HandleKeys(Document& document)
	{
		// Asked every frame for the routing, as undo is, but left alone while
		// the Centre field's text box has the keyboard or the mouse is in a
		// drag, which holds on to what is under it.
		const bool idle = !ImGui::GetIO().WantTextInput && (!document.parts.empty() || !document.joints.empty()) && document.drag == Drag::None;

		for (const PickInfo& info : picks)
			if (ImGui::Shortcut(info.key) && idle)
				SetPick(info.pick);

		for (const ToolInfo& info : tools)
			if (ImGui::Shortcut(info.key) && idle)
				m_tool = info.tool;

		if ((ImGui::Shortcut(ImGuiKey_Delete) || ImGui::Shortcut(ImGuiKey_Backspace)) && idle)
			Delete();

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_E) && idle)
			Extrude();

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D) && idle)
			Duplicate();

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A) && idle)
			SelectAll();

		if (ImGui::Shortcut(ImGuiKey_Escape) && idle)
			SelectNone();

		if (ImGui::Shortcut(ImGuiKey_F) && !ImGui::GetIO().WantTextInput && document.drag == Drag::None)
			Frame(document);
	};

	void ModelEditor::DrawToolbar(Document& document)
	{
		const bool shaping = Shaping(document);

		ImGui::BeginDisabled(document.parts.empty() && document.joints.empty());

		for (const PickInfo& info : picks)
		{
			ImGui::BeginDisabled(info.pick == Pick::Joints ? document.joints.empty() : document.parts.empty());

			if (ImGui::RadioButton(info.label, document.pick == info.pick))
				SetPick(info.pick);

			ImGui::EndDisabled();
			ImGui::SetItemTooltip("%s (%s)", info.tip, ImGui::GetKeyName(info.key));
			ImGui::SameLine();
		};

		ImGui::TextDisabled("|");
		ImGui::SameLine();

		for (const ToolInfo& info : tools)
		{
			if (ImGui::RadioButton(info.label, m_tool == info.tool))
				m_tool = info.tool;

			ImGui::SetItemTooltip("%s (%s)", info.tip, ImGui::GetKeyName(info.key));
			ImGui::SameLine();
		};

		ImGui::EndDisabled();

		ImGui::BeginDisabled(!shaping);

		ImGui::TextDisabled("|");
		ImGui::SameLine();
		ImGui::Checkbox("See Through", &m_seeThrough);
		ImGui::SetItemTooltip("Show and pick what is behind the front of the model too");

		const bool faces = shaping && !PickedFaces(document).empty();
		const bool any = shaping && !Moving(document).empty();

		const auto action = [](const char* label, bool usable, const char* tip, const char* unusable, const char* key)
		{
			ImGui::BeginDisabled(!usable);
			const bool pressed = ImGui::Button(label);
			ImGui::EndDisabled();

			ImGui::SetItemTooltip("%s (%s)", usable ? tip : unusable, key);
			ImGui::SameLine();

			return pressed;
		};

		if (action("Extrude", faces, "Pull the picked faces out, walled in from where they were", "Pick faces to extrude first", "Ctrl+E"))
			Extrude();

		if (action("Duplicate", faces, "Copy the picked faces, to move the copy", "Pick faces to duplicate first", "Ctrl+D"))
			Duplicate();

		if (action("Flip", faces, "Turn the picked faces to face the other way", "Pick faces to flip first", "no key"))
			Flip();

		if (action("Delete", any, "Delete what is picked, and every face it is part of", "Pick something to delete first", "Delete"))
			Delete();

		if (ImGui::Button("+ Add"))
			ImGui::OpenPopup("##add");

		ImGui::SetItemTooltip("Add a shape where the view is looking, picked so it can be moved");

		if (ImGui::BeginPopup("##add"))
		{
			for (const Shapes::Info& info : Shapes::All())
				if (ImGui::MenuItem(info.label))
					Add(info.shape);

			ImGui::EndPopup();
		};

		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		if (action("Undo", !document.undo.empty(), "Take back the last change", "Nothing to take back", "Ctrl+Z"))
			Step(document, true);

		if (action("Redo", !document.redo.empty(), "Make the change taken back again", "Nothing to make again", "Ctrl+Y"))
			Step(document, false);

		if (action("Save", document.unsaved, "Write the meshes and joints back into the prefab", "Nothing to save", "Ctrl+S"))
			SaveDocument(document);

		ImGui::TextDisabled("|");
		ImGui::SameLine();

		if (ImGui::Button("?"))
			ImGui::OpenPopup("##controls");

		ImGui::SetItemTooltip("Controls");

		if (ImGui::BeginPopup("##controls"))
		{
			ImGui::SeparatorText("Controls");
			ImGui::TextUnformatted(controls);
			ImGui::EndPopup();
		};
	};

	void ModelEditor::DrawSide(Document& document)
	{
		const float status_height = ImGui::GetTextLineHeightWithSpacing();

		ImGui::BeginChild("##side", ImVec2(side_width, ImGui::GetContentRegionAvail().y - status_height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

		const bool shaping = Shaping(document);

		if (shaping && document.parts.size() > 1)
		{
			ImGui::SeparatorText("Parts");

			for (size_t i = 0; i < document.parts.size(); i++)
			{
				ImGui::PushID((int)i);

				if (ImGui::Selectable(document.parts[i].object->GetName().c_str(), document.part == i) && document.part != i)
				{
					document.part = i;
					document.selection = { };
				};

				ImGui::PopID();
			};
		};

		if (shaping || document.pick == Pick::Joints)
		{
			ImGui::SeparatorText("Picked");

			const size_t count = document.pick == Pick::Vertices ? document.selection.vertices.size()
				: document.pick == Pick::Edges ? document.selection.edges.size()
				: document.pick == Pick::Joints ? document.selection.joints.size()
				: document.selection.faces.size();

			if (count == 0)
				ImGui::TextDisabled(document.pick == Pick::Joints ? "Nothing. Click a joint." : "Nothing. Click the model.");
			else ImGui::Text("%zu %s", count, PickName(document.pick, count));

			if (std::optional<glm::vec3> pivot = Pivot(document))
			{
				glm::vec3 centre = *pivot;
				const bool changed = ImGui::DragFloat3("Centre", &centre.x, centre_drag_speed);

				if (ImGui::IsItemActivated())
					document.pending = Take(document);

				ImGui::SetItemTooltip("Where the middle of what is picked is. Drag or type to move it.");

				if (changed && shaping)
				{
					Reshape(document.parts[document.part], Moving(document), glm::translate(glm::mat4(1.0f), centre - *pivot));
					Changed(document);
				}
				else if (changed)
				{
					TransformJoints(document, glm::translate(glm::mat4(1.0f), centre - *pivot));
					Posed(document);
				};

				if (ImGui::IsItemDeactivated() && Differs(document, document.pending))
					Commit(document, std::move(document.pending));
			};
		};

		if (shaping)
		{
			const EditableMesh& mesh = document.parts[document.part].editing;

			ImGui::SeparatorText("Mesh");
			ImGui::Text("%zu vertices, %zu faces", mesh.vertices.size(), mesh.faces.size());
		};

		if (document.pick == Pick::Joints)
		{
			ImGui::SeparatorText("Joints");

			for (uint32_t i = 0; i < document.joints.size(); i++)
			{
				const Joint& joint = document.joints[i];
				const float indent = joint.depth * ImGui::GetStyle().IndentSpacing;

				ImGui::PushID((int)i);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);

				if (ImGui::Selectable(joint.object->GetName().c_str(), document.selection.joints.contains(i)))
				{
					if (!ImGui::GetIO().KeyShift)
						document.selection.joints = { i };
					else if (!document.selection.joints.erase(i))
						document.selection.joints.insert(i);
				};

				ImGui::PopID();
			};
		};

		std::vector<Material*> materials{ };

		if (document.root)
			MaterialsOf(*document.root, materials);

		if (!materials.empty())
		{
			ImGui::SeparatorText("Shader");

			const std::string& current = materials.front()->GetShaderPath();
			const bool mixed = std::any_of(materials.begin(), materials.end(), [&](const Material* material) { return material->GetShaderPath() != current; });

			ImGui::SetNextItemWidth(-FLT_MIN);

			if (ImGui::BeginCombo("##shader", mixed ? "Mixed" : current.c_str()))
			{
				if (ImGui::IsWindowAppearing())
					m_shaders = ProjectShaders(document.path);

				for (const std::string& shader : m_shaders)
					if (ImGui::Selectable(shader.c_str(), !mixed && shader == current))
						SetShader(document, shader);

				ImGui::EndCombo();
			};
		};

		if (!document.fixed.empty())
		{
			const char* format = document.parts.empty() ?
				"%zu cannot be shaped here: made by a script, or not triangles." :
				"%zu more cannot be shaped here: made by a script, or not triangles.";

			ImGui::PushTextWrapPos();
			ImGui::TextDisabled(format, document.fixed.size());
			ImGui::PopTextWrapPos();
		};

		ImGui::EndChild();
	};

	void ModelEditor::DrawStatus(const Document& document)
	{
		if (!document.message.empty() && ImGui::GetTime() - document.messageTime < message_seconds)
		{
			ImGui::TextUnformatted(document.message.c_str());
			return;
		};

		const char* hint = "Click the model to pick part of it, or drag a box round several. Right drag orbits, the wheel zooms.";

		if (document.parts.empty() && document.joints.empty())
			hint = "Right drag orbits, middle drag pans, the wheel zooms.";
		else if (document.drag == Drag::Box)
			hint = "Let go to pick everything in the box. Hold Shift to add to what is picked.";
		else if (document.drag == Drag::Handle)
			hint = "Hold Ctrl to snap. Press Esc to put it back.";
		else if (document.hoveredHandle)
			hint = document.hoveredHandle->tool == Tool::Move ? "Drag to move what is picked." :
				document.hoveredHandle->tool == Tool::Rotate ? "Drag round to turn what is picked." :
				document.hoveredHandle->axis ? "Drag out along the arrow to make what is picked bigger, in to make it smaller." :
				"Drag right or up to make what is picked bigger, left or down to make it smaller.";
		else if ((document.hoveredVertex || document.hoveredEdge || document.hoveredFace) && document.hoveredPart != document.part)
			hint = "Click to edit this part, picking this.";
		else if (document.hoveredVertex || document.hoveredEdge || document.hoveredFace || document.hoveredJoint)
			hint = "Click to pick this. Shift + click adds it to, or takes it from, what is picked.";
		else if (document.pick == Pick::Joints)
			hint = "Click a joint to pick it, or drag a box round several. Right drag orbits, the wheel zooms.";

		ImGui::TextDisabled("%s", hint);
	};

	void ModelEditor::DrawCloseQuestion()
	{
		constexpr const char* popup = "Unsaved Model";

		if (m_closing != nullptr && !ImGui::IsPopupOpen(popup))
			ImGui::OpenPopup(popup);

		if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		if (m_closing == nullptr)
		{
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		};

		ImGui::Text("Save the changes to %s?", std::filesystem::path(m_closing->path).filename().string().c_str());

		const bool save = ImGui::Button("Save");
		ImGui::SameLine();
		const bool discard = ImGui::Button("Don't Save");
		ImGui::SameLine();
		const bool cancel = ImGui::Button("Cancel");

		// A save that fails leaves the question up, with the edits still open.
		const bool saved = save && SaveDocument(*m_closing);

		if (saved || discard)
			Close(m_closing);

		if (saved || discard || cancel)
		{
			m_closing = nullptr;
			ImGui::CloseCurrentPopup();
		};

		ImGui::EndPopup();
	};

	void ModelEditor::Close(const Document* document)
	{
		if (document == nullptr)
			return;

		if (m_front == document)
			m_front = nullptr;

		for (const std::unique_ptr<Document>& open : m_documents)
			if (open.get() == document)
				Unload(*open);

		std::erase_if(m_documents, [document](const std::unique_ptr<Document>& open) { return open.get() == document; });
	};

	void ModelEditor::DrawCanvas(Document& document, const ImVec2& size)
	{
		if (size.x < 1.0f || size.y < 1.0f)
			return;

		const ImVec2 corner = ImGui::GetCursorScreenPos();

		ImGui::InvisibleButton("##model", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

		const bool hovered = ImGui::IsItemHovered();

		// With only fixed meshes and no joints the view turns round them, and
		// nothing is picked.
		const bool editable = !document.parts.empty() || !document.joints.empty();

		// The camera is steered before anything is measured from it, so the
		// image and the picking keep up with the mouse.
		Steer(document, hovered);
		Pose(document);
		Measure(document, corner, size);

		const ImGuiIO& io = ImGui::GetIO();
		const ImVec2 mouse = io.MousePos;

		if (document.drag == Drag::None)
		{
			if (hovered && editable)
				Hover(document, mouse);
			else
			{
				document.hoveredHandle.reset();
				document.hoveredVertex.reset();
				document.hoveredEdge.reset();
				document.hoveredFace.reset();
				document.hoveredJoint.reset();
			};
		};

		if (hovered && editable && document.drag == Drag::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
		{
			document.pressed = mouse;

			if (document.hoveredHandle)
			{
				document.pending = Take(document);
				document.wasUnsaved = document.unsaved;
				document.held = *document.hoveredHandle;
				document.pivot = Pivot(document).value_or(glm::vec3(0.0f));
				document.drag = Drag::Handle;
			}
			else document.drag = Drag::Click;
		};

		if (document.drag == Drag::Click && Distance(mouse, document.pressed) > drag_threshold)
			document.drag = Drag::Box;

		if (document.drag == Drag::Handle)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				Restore(document, document.pending);
				document.unsaved = document.wasUnsaved;
				document.drag = Drag::None;
			}
			else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				DragHandle(document, mouse);
		};

		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			if (document.drag == Drag::Click)
				Click(document, document.pressed);
			else if (document.drag == Drag::Box)
				Box(document, document.pressed, mouse);
			else if (document.drag == Drag::Handle && Differs(document, document.pending))
				Commit(document, std::move(document.pending));
			else if (document.drag == Drag::Handle)
				document.unsaved = document.wasUnsaved;

			if (document.drag == Drag::Click || document.drag == Drag::Box || document.drag == Drag::Handle)
				document.drag = Drag::None;
		};

		Render(document);

		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 end(corner.x + size.x, corner.y + size.y);

		if (document.viewport.GetTexture())
			draw->AddImage(
				(ImTextureID)document.viewport.GetTextureID(),
				corner,
				end,
				ImVec2(0.0f, 1.0f),	// The framebuffer is bottom-up,
				ImVec2(1.0f, 0.0f));// so the V axis is flipped

		draw->PushClipRect(corner, end, true);

		if (editable)
			DrawHandles(document, draw);

		if (document.drag == Drag::Box)
		{
			const ImVec2 low((std::min)(document.pressed.x, mouse.x), (std::min)(document.pressed.y, mouse.y));
			const ImVec2 high((std::max)(document.pressed.x, mouse.x), (std::max)(document.pressed.y, mouse.y));

			draw->AddRectFilled(low, high, box_fill);
			draw->AddRect(low, high, box_border);
		};

		draw->PopClipRect();
	};

	void ModelEditor::Steer(Document& document, bool hovered)
	{
		const ImGuiIO& io = ImGui::GetIO();

		if (hovered && io.MouseWheel != 0.0f)
			document.distance = (std::max)(min_distance, document.distance * std::pow(zoom_per_notch, io.MouseWheel));

		if (hovered && document.drag == Drag::None)
		{
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
				document.drag = Drag::Orbit;
			else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
				document.drag = Drag::Pan;
		};

		if (document.drag == Drag::Orbit)
		{
			document.yaw -= io.MouseDelta.x * orbit_degrees_per_pixel;
			document.pitch = std::clamp(document.pitch - io.MouseDelta.y * orbit_degrees_per_pixel, -max_pitch, max_pitch);

			if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
				document.drag = Drag::None;
		}
		else if (document.drag == Drag::Pan)
		{
			const float scale = document.view.pixel * document.distance;

			document.target += (document.view.up * io.MouseDelta.y - document.view.right * io.MouseDelta.x) * scale;

			if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
				document.drag = Drag::None;
		};
	};

	void ModelEditor::Pose(Document& document)
	{
		const glm::mat4 rotation =
			glm::rotate(glm::mat4(1.0f), glm::radians(document.yaw), Y_AXIS) *
			glm::rotate(glm::mat4(1.0f), glm::radians(document.pitch), X_AXIS);

		const glm::vec3 eye = document.target + glm::vec3(rotation * glm::vec4(0.0f, 0.0f, document.distance, 0.0f));

		document.camera.nearPlane = document.distance * near_share;
		document.camera.farPlane = document.distance * far_share;
		document.camera.SetPose(glm::translate(glm::mat4(1.0f), eye) * rotation);

		document.view.eye = eye;
		document.view.right = glm::vec3(rotation[0]);
		document.view.up = glm::vec3(rotation[1]);
	};

	void ModelEditor::Measure(Document& document, const ImVec2& corner, const ImVec2& size)
	{
		View& view = document.view;

		view.corner = corner;
		view.size = size;
		view.viewProjection = document.camera.ViewProjection(size.x / size.y);
		view.pixel = 2.0f * std::tan(glm::radians((float)document.camera.fieldOfView) / 2.0f) / size.y;
		view.world = Shaping(document) ? document.parts[document.part].object->WorldMatrix() : glm::mat4(1.0f);
		view.local = glm::inverse(view.world);

		// The joints may have moved since, posed or played.
		if (Shaping(document))
			for (Part& part : document.parts)
				Bend(part);
	};

	void ModelEditor::Frame(Document& document)
	{
		// What is picked, or with nothing picked every part.
		const std::set<uint32_t> picked = Shaping(document) ? Moving(document) : std::set<uint32_t>{ };
		std::vector<glm::vec3> framed{ };

		if (document.pick == Pick::Joints)
			for (const uint32_t joint : document.selection.joints)
				framed.push_back(Placed(*document.joints[joint].object));

		const bool whole = picked.empty() && framed.empty();

		for (size_t i = 0; i < document.parts.size(); i++)
		{
			const Part& part = document.parts[i];
			const glm::mat4 world = part.object->WorldMatrix();

			if (whole)
				for (const glm::vec3& vertex : Shown(part))
					framed.push_back(glm::vec3(world * glm::vec4(vertex, 1.0f)));
			else if (i == document.part)
				for (const uint32_t vertex : picked)
					framed.push_back(glm::vec3(world * glm::vec4(Shown(part)[vertex], 1.0f)));
		};

		if (whole)
			for (GameObject* object : document.fixed)
			{
				const glm::mat4 world = object->WorldMatrix();
				const std::vector<float>& vertices = *std::as_const(object->GetComponent<Mesh>()->m_vertices);

				for (size_t i = 0; i + floats_per_position <= vertices.size(); i += floats_per_position)
					framed.push_back(glm::vec3(world * glm::vec4(vertices[i], vertices[i + 1], vertices[i + 2], 1.0f)));
			};

		if (framed.empty())
		{
			document.target = glm::vec3(0.0f);
			document.distance = empty_distance;
			return;
		};

		glm::vec3 low = framed.front();
		glm::vec3 high = low;

		for (const glm::vec3& point : framed)
		{
			low = glm::min(low, point);
			high = glm::max(high, point);
		};

		const float radius = glm::length(high - low) / 2.0f;
		const float half_view = glm::radians((float)document.camera.fieldOfView) / 2.0f;

		document.target = (low + high) / 2.0f;

		// A point alone, such as one joint, is looked at from as far as before.
		if (radius > 0.0f)
			document.distance = (std::max)(min_distance, radius / std::sin(half_view) * fit_margin);
	};

	std::optional<ImVec2> ModelEditor::Project(const View& view, const glm::vec3& world) const
	{
		const glm::vec4 clip = view.viewProjection * glm::vec4(world, 1.0f);

		if (clip.w <= 0.0f)
			return std::nullopt;

		const glm::vec3 device = glm::vec3(clip) / clip.w;

		return ImVec2(
			view.corner.x + (device.x + 1.0f) / 2.0f * view.size.x,
			view.corner.y + (1.0f - device.y) / 2.0f * view.size.y);
	};

	void ModelEditor::Ray(const View& view, const ImVec2& mouse, glm::vec3& origin, glm::vec3& direction) const
	{
		const glm::vec2 device(
			(mouse.x - view.corner.x) / view.size.x * 2.0f - 1.0f,
			1.0f - (mouse.y - view.corner.y) / view.size.y * 2.0f);

		const glm::vec4 distant = glm::inverse(view.viewProjection) * glm::vec4(device, 1.0f, 1.0f);

		origin = view.eye;
		direction = glm::normalize(glm::vec3(distant) / distant.w - view.eye);
	};

	bool ModelEditor::Visible(const Document& document, const glm::vec3& point) const
	{
		if (m_seeThrough)
			return true;

		// Against every part, each in its own space along the same ray from
		// the eye, so another part in front hides it too.
		for (const Part& part : document.parts)
		{
			const glm::mat4 inverse = glm::inverse(part.object->WorldMatrix());
			const glm::vec3 eye(inverse * glm::vec4(document.view.eye, 1.0f));
			const std::optional<EditableMesh::Hit> hit = part.editing.Raycast(eye, glm::vec3(inverse * glm::vec4(point, 1.0f)) - eye, Shown(part));

			if (hit && hit->distance < 1.0f - seen_share)
				return false;
		};

		return true;
	};

	std::set<uint32_t> ModelEditor::Moving(const Document& document) const
	{
		const EditableMesh& mesh = document.parts[document.part].editing;

		switch (document.pick)
		{
		case Pick::Vertices:	return document.selection.vertices;
		case Pick::Edges:		return mesh.VerticesOfEdges(document.selection.edges);
		default:				return mesh.VerticesOfFaces(document.selection.faces);
		};
	};

	std::optional<glm::vec3> ModelEditor::Pivot(const Document& document) const
	{
		if (document.pick == Pick::Joints)
		{
			if (document.selection.joints.empty())
				return std::nullopt;

			glm::vec3 sum(0.0f);

			for (const uint32_t joint : document.selection.joints)
				sum += Placed(*document.joints[joint].object);

			return sum / (float)document.selection.joints.size();
		};

		const std::set<uint32_t> moving = Moving(document);

		if (moving.empty())
			return std::nullopt;

		glm::vec3 sum(0.0f);

		for (const uint32_t vertex : moving)
			sum += Shown(document.parts[document.part])[vertex];

		return sum / (float)moving.size();
	};

	const std::set<uint32_t>& ModelEditor::PickedFaces(Document& document) const
	{
		const std::pair<size_t, Pick> of(document.part, document.pick);

		if (document.pickedOf != of || document.pickedFrom != document.selection)
		{
			document.pickedFaces = document.pick == Pick::Faces
				? document.selection.faces
				: document.parts[document.part].editing.FacesWithin(Moving(document));
			document.pickedOf = of;
			document.pickedFrom = document.selection;
		};

		return document.pickedFaces;
	};

	std::optional<ModelEditor::Handle> ModelEditor::HandleAt(const Document& document, const ImVec2& mouse) const
	{
		const std::optional<glm::vec3> pivot = Pivot(document);

		if (!pivot)
			return std::nullopt;

		const View& view = document.view;
		const glm::vec3 centre(view.world * glm::vec4(*pivot, 1.0f));
		const std::optional<ImVec2> middle = Project(view, centre);

		if (!middle)
			return std::nullopt;

		const float length = handle_length * view.pixel * glm::distance(view.eye, centre);
		const float off_centre = Distance(mouse, *middle);

		if (m_tool == Tool::Rotate)
		{
			if (std::abs(off_centre - handle_length * view_ring_share) < handle_hit)
				return Handle{ m_tool, std::nullopt };
		}
		else if (off_centre < centre_size + handle_hit / 2.0f)
			return Handle{ m_tool, std::nullopt };

		std::optional<Handle> nearest{ };
		float nearest_distance = handle_hit;

		for (int axis = 0; axis < axes; axis++)
		{
			const glm::vec3 direction = glm::normalize(glm::mat3(view.world) * axis_vectors[axis]);
			float distance = handle_hit;

			if (m_tool == Tool::Rotate)
			{
				glm::vec3 u;
				glm::vec3 v;
				Around(direction, u, v);

				std::optional<ImVec2> last{ };

				for (int i = 0; i <= ring_points; i++)
				{
					const float angle = glm::two_pi<float>() * i / ring_points;
					const std::optional<ImVec2> point = Project(view, centre + (u * std::cos(angle) + v * std::sin(angle)) * length);

					if (point && last)
						distance = (std::min)(distance, SegmentDistance(mouse, *last, *point));

					last = point;
				};
			}
			else if (const std::optional<ImVec2> tip = Project(view, centre + direction * length))
				distance = SegmentDistance(mouse, *middle, *tip);

			if (distance < nearest_distance)
			{
				nearest_distance = distance;
				nearest = Handle{ m_tool, axis };
			};
		};

		return nearest;
	};

	void ModelEditor::Hover(Document& document, const ImVec2& mouse)
	{
		document.hoveredHandle = HandleAt(document, mouse);
		document.hoveredVertex.reset();
		document.hoveredEdge.reset();
		document.hoveredFace.reset();
		document.hoveredJoint.reset();

		if (document.hoveredHandle)
			return;

		const View& view = document.view;

		// Drawn in front of the model, so always seen.
		if (document.pick == Pick::Joints)
		{
			float nearest = pick_radius;

			for (uint32_t joint = 0; joint < document.joints.size(); joint++)
				if (const std::optional<ImVec2> point = Project(view, Placed(*document.joints[joint].object)))
					if (const float distance = Distance(mouse, *point); distance < nearest)
					{
						nearest = distance;
						document.hoveredJoint = joint;
					};

			return;
		};

		// Every part, nearest first, so a click on another goes on to edit it.
		if (document.pick == Pick::Faces)
		{
			glm::vec3 origin;
			glm::vec3 direction;
			Ray(view, mouse, origin, direction);

			// Each is hit in its own space along the same ray, so how far along
			// it compares across them.
			std::optional<EditableMesh::Hit> nearest{ };

			for (size_t i = 0; i < document.parts.size(); i++)
			{
				const glm::mat4 local = glm::inverse(document.parts[i].object->WorldMatrix());
				const std::optional<EditableMesh::Hit> hit = document.parts[i].editing.Raycast(glm::vec3(local * glm::vec4(origin, 1.0f)), glm::vec3(local * glm::vec4(direction, 0.0f)), Shown(document.parts[i]));

				if (hit && (!nearest || hit->distance < nearest->distance))
				{
					nearest = hit;
					document.hoveredFace = hit->face;
					document.hoveredPart = i;
				};
			};

			return;
		};

		// Nearest first, so only those in reach are tested for being seen.
		if (document.pick == Pick::Vertices)
		{
			std::vector<std::tuple<float, size_t, uint32_t, glm::vec3>> found{ };

			for (size_t i = 0; i < document.parts.size(); i++)
			{
				const glm::mat4 world = document.parts[i].object->WorldMatrix();
				const std::vector<glm::vec3>& shown = Shown(document.parts[i]);

				for (uint32_t vertex = 0; vertex < shown.size(); vertex++)
				{
					const glm::vec3 placed(world * glm::vec4(shown[vertex], 1.0f));

					if (const std::optional<ImVec2> point = Project(view, placed))
						if (const float distance = Distance(mouse, *point); distance < pick_radius)
							found.push_back({ distance, i, vertex, placed });
				};
			};

			std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });

			for (const auto& [distance, part, vertex, placed] : found)
				if (Visible(document, placed))
				{
					document.hoveredVertex = vertex;
					document.hoveredPart = part;
					return;
				};

			return;
		};

		std::vector<std::tuple<float, size_t, EditableMesh::Edge, glm::vec3>> found{ };

		for (size_t i = 0; i < document.parts.size(); i++)
		{
			const glm::mat4 world = document.parts[i].object->WorldMatrix();
			const std::vector<glm::vec3>& shown = Shown(document.parts[i]);

			for (const EditableMesh::Edge& edge : Edges(document.parts[i]))
			{
				const glm::vec3 from(world * glm::vec4(shown[edge.first], 1.0f));
				const glm::vec3 to(world * glm::vec4(shown[edge.second], 1.0f));
				const std::optional<ImVec2> a = Project(view, from);
				const std::optional<ImVec2> b = Project(view, to);

				if (a && b)
					if (const float distance = SegmentDistance(mouse, *a, *b); distance < pick_radius)
						found.push_back({ distance, i, edge, (from + to) / 2.0f });
			};
		};

		std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });

		for (const auto& [distance, part, edge, middle] : found)
			if (Visible(document, middle))
			{
				document.hoveredEdge = edge;
				document.hoveredPart = part;
				return;
			};
	};

	void ModelEditor::Click(Document& document, const ImVec2& mouse)
	{
		const ImGuiIO& io = ImGui::GetIO();
		const bool adding = io.KeyShift;
		Selection& selection = document.selection;

		const auto toggle = [&](auto& set, const auto& element)
		{
			if (!adding)
				set = { element };
			else if (!set.erase(element))
				set.insert(element);
		};

		Hover(document, mouse);

		if ((document.hoveredVertex || document.hoveredEdge || document.hoveredFace) && document.hoveredPart != document.part)
		{
			document.part = document.hoveredPart;
			selection = { };
		};

		if (document.hoveredJoint)
			toggle(selection.joints, *document.hoveredJoint);
		else if (document.hoveredVertex)
			toggle(selection.vertices, *document.hoveredVertex);
		else if (document.hoveredEdge)
			toggle(selection.edges, *document.hoveredEdge);
		else if (document.hoveredFace)
			toggle(selection.faces, *document.hoveredFace);
		else if (!adding)
			selection = { };
	};

	void ModelEditor::Box(Document& document, const ImVec2& from, const ImVec2& to)
	{
		const ImVec2 low((std::min)(from.x, to.x), (std::min)(from.y, to.y));
		const ImVec2 high((std::max)(from.x, to.x), (std::max)(from.y, to.y));

		const View& view = document.view;

		const auto inside = [&](const glm::vec3& local)
		{
			const std::optional<ImVec2> point = Project(view, glm::vec3(view.world * glm::vec4(local, 1.0f)));

			return point && point->x >= low.x && point->x <= high.x && point->y >= low.y && point->y <= high.y;
		};

		Selection& selection = document.selection;

		if (!ImGui::GetIO().KeyShift)
			selection = { };

		if (document.pick == Pick::Joints)
		{
			for (uint32_t joint = 0; joint < document.joints.size(); joint++)
				if (inside(Placed(*document.joints[joint].object)))
					selection.joints.insert(joint);

			return;
		};

		Part& part = document.parts[document.part];
		const EditableMesh& mesh = part.editing;
		const std::vector<glm::vec3>& shown = Shown(part);
		const auto seen = [&](const glm::vec3& local) { return Visible(document, glm::vec3(view.world * glm::vec4(local, 1.0f))); };

		switch (document.pick)
		{
		case Pick::Vertices:
			for (uint32_t vertex = 0; vertex < shown.size(); vertex++)
				if (inside(shown[vertex]) && seen(shown[vertex]))
					selection.vertices.insert(vertex);
			break;

		case Pick::Edges:
			for (const EditableMesh::Edge& edge : Edges(part))
				if (inside(shown[edge.first]) && inside(shown[edge.second]) && seen((shown[edge.first] + shown[edge.second]) / 2.0f))
					selection.edges.insert(edge);
			break;

		default:
			for (uint32_t face = 0; face < mesh.faces.size(); face++)
				if (const glm::vec3 centre = mesh.FaceCentre(face, shown); inside(centre) && seen(centre))
					selection.faces.insert(face);
			break;
		};
	};

	void ModelEditor::DragHandle(Document& document, const ImVec2& mouse)
	{
		const View& view = document.view;
		const bool snapping = ImGui::GetIO().KeyCtrl;
		const glm::vec3 centre(view.world * glm::vec4(document.pivot, 1.0f));
		const std::optional<ImVec2> middle = Project(view, centre);

		if (!middle)
			return;

		const std::optional<int> axis = document.held.axis;
		const glm::vec3 direction = axis ? glm::normalize(glm::mat3(view.world) * axis_vectors[*axis]) : glm::normalize(view.eye - centre);

		glm::mat4 change(1.0f);

		if (document.held.tool == Tool::Move)
		{
			glm::vec3 start_origin;
			glm::vec3 start_direction;
			glm::vec3 origin;
			glm::vec3 ray;
			Ray(view, document.pressed, start_origin, start_direction);
			Ray(view, mouse, origin, ray);

			glm::vec3 moved(0.0f);

			if (axis)
			{
				// Where along the axis the mouse's ray passes closest to it.
				const auto along = [&](const glm::vec3& from, const glm::vec3& towards)
				{
					const float b = glm::dot(direction, towards);
					const float denominator = 1.0f - b * b;
					const glm::vec3 between = centre - from;

					return denominator > 1e-6f ? (b * glm::dot(towards, between) - glm::dot(direction, between)) / denominator : 0.0f;
				};

				float distance = along(origin, ray) - along(start_origin, start_direction);

				if (snapping)
					distance = Snap(distance, move_snap);

				moved = direction * distance;
			}
			else
			{
				// Across the plane through the middle that faces the eye.
				const auto onto = [&](const glm::vec3& from, const glm::vec3& towards)
				{
					const float facing = glm::dot(towards, direction);

					return facing != 0.0f ? from + towards * (glm::dot(centre - from, direction) / facing) : centre;
				};

				moved = onto(origin, ray) - onto(start_origin, start_direction);

				if (snapping)
					moved = glm::vec3(Snap(moved.x, move_snap), Snap(moved.y, move_snap), Snap(moved.z, move_snap));
			};

			change = glm::translate(glm::mat4(1.0f), moved);
		}
		else if (document.held.tool == Tool::Rotate)
		{
			const float start = std::atan2(document.pressed.y - middle->y, document.pressed.x - middle->x);
			const float now = std::atan2(mouse.y - middle->y, mouse.x - middle->x);

			// Screen angles grow clockwise, as y grows down. Seen from the
			// side the axis points to, a positive turn is anticlockwise.
			float degrees = glm::degrees(start - now);

			if (glm::dot(direction, view.eye - centre) < 0.0f)
				degrees = -degrees;

			if (snapping)
				degrees = Snap(degrees, turn_snap);

			change = glm::translate(glm::mat4(1.0f), centre) * glm::rotate(glm::mat4(1.0f), glm::radians(degrees), direction) * glm::translate(glm::mat4(1.0f), -centre);
		}
		else
		{
			const ImVec2 from(document.pressed.x - middle->x, document.pressed.y - middle->y);
			const ImVec2 to(mouse.x - middle->x, mouse.y - middle->y);

			float factor = 1.0f;
			glm::mat4 sizing(1.0f);

			if (axis)
			{
				const std::optional<ImVec2> tip = Project(view, centre + direction);

				if (tip)
				{
					const glm::vec2 across = glm::vec2(tip->x - middle->x, tip->y - middle->y);
					const float reach = glm::length(across) > 0.0f ? glm::dot(glm::vec2(from.x, from.y), glm::normalize(across)) : 0.0f;

					if (std::abs(reach) >= 1.0f)
						factor = glm::dot(glm::vec2(to.x, to.y), glm::normalize(across)) / reach;
				};

				if (snapping)
					factor = Snap(factor, scale_snap);

				factor = (std::max)(min_scale, factor);
				sizing = glm::mat4(glm::mat3(1.0f) + (factor - 1.0f) * glm::outerProduct(direction, direction));
			}
			else
			{
				// Grabbed at the middle, so by how far right and up it has gone
				// rather than out from there: a handle's length doubles it.
				factor = 1.0f + ((mouse.x - document.pressed.x) - (mouse.y - document.pressed.y)) / handle_length;

				if (snapping)
					factor = Snap(factor, scale_snap);

				factor = (std::max)(min_scale, factor);
				sizing = glm::scale(glm::mat4(1.0f), glm::vec3(factor));
			};

			change = glm::translate(glm::mat4(1.0f), centre) * sizing * glm::translate(glm::mat4(1.0f), -centre);
		};

		if (document.pick == Pick::Joints)
		{
			SetPoses(document, document.pending.poses);
			TransformJoints(document, change);
			Posed(document);
			return;
		};

		Part& part = document.parts[document.part];
		part.editing = document.pending.mesh;
		Reshape(part, Moving(document), view.local * change * view.world);

		Changed(document);
	};

	void ModelEditor::Render(Document& document)
	{
		if (Renderer::Get() == nullptr || document.scene == nullptr)
			return;

		document.viewport.Render(
			(int)document.view.size.x,
			(int)document.view.size.y,
			Engine::clearColor,
			[&]()
			{
				document.scene->Render(&document.camera, { document.scene });

				if (Shaping(document) || document.pick == Pick::Joints)
					DrawOverlay(document);
			});
	};

	void ModelEditor::DrawOverlay(Document& document)
	{
		Renderer* renderer = Renderer::Get();

		if (!Compile())
			return;

		const View& view = document.view;
		const bool joints = document.pick == Pick::Joints;

		// Of any part, so the face under the mouse lights up on a part not
		// being edited too.
		const auto fill = [&](const Part& part, const std::set<uint32_t>& faces)
		{
			const glm::mat4 world = part.object->WorldMatrix();
			const std::vector<glm::vec3>& shown = Shown(part);
			std::vector<glm::vec3> triangles{ };

			for (const uint32_t face : faces)
			{
				const std::vector<EditableMesh::Corner>& corners = part.editing.faces[face].corners;

				for (size_t i = 2; i < corners.size(); i++)
					for (const size_t corner : { (size_t)0, i - 1, i })
						triangles.push_back(glm::vec3(world * glm::vec4(shown[corners[corner].vertex], 1.0f)));
			};

			return triangles;
		};

		// Edges as strips facing the eye, so they keep their width on screen
		// whichever renderer draws them.
		const auto strip = [&](const glm::vec3& a, const glm::vec3& b, float half_width, std::vector<glm::vec3>& triangles)
		{
			const glm::vec3 side = glm::cross(b - a, view.eye - (a + b) / 2.0f);

			if (glm::length(side) == 0.0f)
				return;

			const glm::vec3 across = glm::normalize(side) * half_width * view.pixel;
			const glm::vec3 at_a = across * glm::distance(view.eye, a);
			const glm::vec3 at_b = across * glm::distance(view.eye, b);

			triangles.insert(triangles.end(), { a - at_a, b - at_b, b + at_b, a - at_a, b + at_b, a + at_a });
		};

		// Spaced a world length apart, each dot keeps to its share of that.
		const auto dots = [&](const std::vector<glm::vec3>& at, float half_size, float spacing = 0.0f)
		{
			std::vector<glm::vec3> triangles{ };

			for (const glm::vec3& p : at)
			{
				const float distance = glm::distance(view.eye, p);
				const float pixels = spacing > 0.0f ? std::clamp(dot_edge_share * spacing / (view.pixel * distance), min_dot_half_size, half_size) : half_size;
				const float size = pixels * view.pixel * distance;
				const glm::vec3 right = view.right * size;
				const glm::vec3 up = view.up * size;

				triangles.insert(triangles.end(), { p - right - up, p + right - up, p + right + up, p - right - up, p + right + up, p - right + up });
			};

			return triangles;
		};

		const auto draw = [&](const std::vector<glm::vec3>& triangles, const glm::vec4& colour)
		{
			if (triangles.empty())
				return;

			renderer->SetUniform(m_program, "u_color", colour);
			renderer->Draw(m_program, GL_TRIANGLES, &triangles[0].x, triangles.size(), GL_STREAM_DRAW);
		};

		// The background laid over the model, as if it were seen through.
		if (joints)
		{
			renderer->Clear(nullptr, true);
			renderer->SetUniform(m_program, "u_viewProjection", glm::mat4(1.0f));
			renderer->SetUniform(m_program, "u_pull", 0.0f);
			draw(std::vector<glm::vec3>(std::begin(screen_corners), std::end(screen_corners)), glm::vec4(Engine::clearColor[0], Engine::clearColor[1], Engine::clearColor[2], fade_alpha));
		};

		if (joints || m_seeThrough)
			renderer->Clear(nullptr, true);

		document.camera.Apply(m_program);
		renderer->SetUniform(m_program, "u_pull", overlay_pull);

		// The ground, its lines a step apart that grows tenfold as the view
		// backs off tenfold, about where the view is looking.
		const float spacing = std::pow(10.0f, std::floor(std::log10(document.distance))) * grid_share;
		const float reach = grid_half_lines * spacing;
		const glm::vec3 middle(Snap(document.target.x, spacing), 0.0f, Snap(document.target.z, spacing));

		std::vector<glm::vec3> grid{ };
		std::vector<glm::vec3> x_axis{ };
		std::vector<glm::vec3> z_axis{ };

		for (int i = -grid_half_lines; i <= grid_half_lines; i++)
		{
			const float step = i * spacing;

			strip(middle + glm::vec3(step, 0.0f, -reach), middle + glm::vec3(step, 0.0f, reach), wire_half_width, grid);
			strip(middle + glm::vec3(-reach, 0.0f, step), middle + glm::vec3(reach, 0.0f, step), wire_half_width, grid);
		};

		strip(glm::vec3(middle.x - reach, 0.0f, 0.0f), glm::vec3(middle.x + reach, 0.0f, 0.0f), picked_wire_half_width, x_axis);
		strip(glm::vec3(0.0f, 0.0f, middle.z - reach), glm::vec3(0.0f, 0.0f, middle.z + reach), picked_wire_half_width, z_axis);

		draw(grid, grid_colour);
		draw(x_axis, glm::vec4(axis_colours[0], grid_axis_alpha));
		draw(z_axis, glm::vec4(axis_colours[axes - 1], grid_axis_alpha));

		if (joints)
		{
			// Each joint's bone, from the joint above it.
			std::vector<glm::vec3> bones{ };
			std::vector<glm::vec3> all{ };
			std::vector<glm::vec3> picked{ };

			for (uint32_t i = 0; i < document.joints.size(); i++)
			{
				const Joint& joint = document.joints[i];
				const glm::vec3 at = Placed(*joint.object);

				for (const GameObject* above = joint.object->GetParent(); above && joint.depth > 0; above = above->GetParent())
					if (std::any_of(document.joints.begin(), document.joints.end(), [above](const Joint& other) { return other.object == above; }))
					{
						strip(Placed(*above), at, picked_wire_half_width, bones);
						break;
					};

				all.push_back(at);

				if (document.selection.joints.contains(i))
					picked.push_back(at);
			};

			draw(bones, bone_colour);
			draw(dots(all, joint_half_size), wire_colour);
			draw(dots(picked, joint_half_size), picked_colour);

			if (document.hoveredJoint)
				draw(dots({ Placed(*document.joints[*document.hoveredJoint].object) }, joint_half_size), hovered_colour);

			return;
		};

		// Where each part's vertices are drawn in the world.
		const auto placed = [](const Part& part)
		{
			const glm::mat4 world = part.object->WorldMatrix();
			std::vector<glm::vec3> at{ };

			for (const glm::vec3& vertex : Shown(part))
				at.push_back(glm::vec3(world * glm::vec4(vertex, 1.0f)));

			return at;
		};

		Part& part = document.parts[document.part];
		const std::vector<glm::vec3> points = placed(part);

		// What is under the mouse may be on another part.
		const bool elsewhere = (document.hoveredVertex || document.hoveredEdge) && document.hoveredPart != document.part;
		const std::vector<glm::vec3> hovered_points = elsewhere ? placed(document.parts[document.hoveredPart]) : points;

		const auto strips = [&](const std::vector<EditableMesh::Edge>& edges, float half_width, const std::vector<glm::vec3>& on)
		{
			std::vector<glm::vec3> triangles{ };

			for (const EditableMesh::Edge& edge : edges)
				strip(on[edge.first], on[edge.second], half_width, triangles);

			return triangles;
		};

		const auto points_of = [&](const std::set<uint32_t>& vertices)
		{
			std::vector<glm::vec3> at{ };

			for (const uint32_t vertex : vertices)
				at.push_back(points[vertex]);

			return at;
		};

		const std::set<uint32_t> moving = Moving(document);
		std::vector<EditableMesh::Edge> picked_edges{ };

		for (const EditableMesh::Edge& edge : Edges(part))
			if (document.pick == Pick::Edges ? document.selection.edges.contains(edge) : moving.contains(edge.first) && moving.contains(edge.second))
				picked_edges.push_back(edge);

		draw(fill(part, PickedFaces(document)), picked_fill);

		if (document.hoveredFace)
			draw(fill(document.parts[document.hoveredPart], { *document.hoveredFace }), hovered_fill);

		draw(strips(Edges(part), wire_half_width, points), wire_colour);
		draw(strips(picked_edges, picked_wire_half_width, points), picked_colour);

		if (document.hoveredEdge)
			draw(strips({ *document.hoveredEdge }, picked_wire_half_width, hovered_points), hovered_colour);

		if (document.pick == Pick::Vertices)
		{
			const auto mean_edge = [](Part& of, const std::vector<glm::vec3>& on)
			{
				const std::vector<EditableMesh::Edge>& edges = Edges(of);
				float total = 0.0f;

				for (const EditableMesh::Edge& edge : edges)
					total += glm::distance(on[edge.first], on[edge.second]);

				return edges.empty() ? 0.0f : total / edges.size();
			};

			const float spacing = mean_edge(part, points);

			draw(dots(points, dot_half_size, spacing), wire_colour);
			draw(dots(points_of(document.selection.vertices), dot_half_size, spacing), picked_colour);

			if (document.hoveredVertex)
				draw(dots({ hovered_points[*document.hoveredVertex] }, dot_half_size, elsewhere ? mean_edge(document.parts[document.hoveredPart], hovered_points) : spacing), hovered_colour);
		};
	};

	void ModelEditor::DrawHandles(const Document& document, ImDrawList* draw) const
	{
		const std::optional<glm::vec3> pivot = document.drag == Drag::Handle && m_tool != Tool::Move ? document.pivot : Pivot(document);

		if (!pivot || document.drag == Drag::Box)
			return;

		const View& view = document.view;
		const glm::vec3 centre(view.world * glm::vec4(*pivot, 1.0f));
		const std::optional<ImVec2> middle = Project(view, centre);

		if (!middle)
			return;

		const std::optional<Handle> lit = document.drag == Drag::Handle ? std::optional<Handle>(document.held) : document.hoveredHandle;
		const float length = handle_length * view.pixel * glm::distance(view.eye, centre);

		for (int axis = 0; axis < axes; axis++)
		{
			const glm::vec3 direction = glm::normalize(glm::mat3(view.world) * axis_vectors[axis]);
			const glm::vec3& shade = axis_colours[axis];
			const ImU32 colour = lit == Handle{ m_tool, axis } ? lit_colour : ImGui::ColorConvertFloat4ToU32(ImVec4(shade.r, shade.g, shade.b, 1.0f));

			if (m_tool == Tool::Rotate)
			{
				glm::vec3 u;
				glm::vec3 v;
				Around(direction, u, v);

				std::vector<ImVec2> ring{ };

				for (int i = 0; i < ring_points; i++)
				{
					const float angle = glm::two_pi<float>() * i / ring_points;

					if (const std::optional<ImVec2> point = Project(view, centre + (u * std::cos(angle) + v * std::sin(angle)) * length))
						ring.push_back(*point);
				};

				if (ring.size() == ring_points)
					draw->AddPolyline(ring.data(), (int)ring.size(), colour, ImDrawFlags_Closed, handle_width);

				continue;
			};

			const std::optional<ImVec2> tip = Project(view, centre + direction * length);

			if (!tip)
				continue;

			draw->AddLine(*middle, *tip, colour, handle_width);

			if (m_tool == Tool::Scale)
				draw->AddRectFilled(ImVec2(tip->x - centre_size / 2.0f, tip->y - centre_size / 2.0f), ImVec2(tip->x + centre_size / 2.0f, tip->y + centre_size / 2.0f), colour);
			else
			{
				const ImVec2 along(tip->x - middle->x, tip->y - middle->y);
				const float reach = std::hypot(along.x, along.y);

				if (reach > 0.0f)
				{
					const ImVec2 unit(along.x / reach, along.y / reach);
					const ImVec2 base(tip->x - unit.x * arrow_size, tip->y - unit.y * arrow_size);
					const ImVec2 side(-unit.y * arrow_size / 2.0f, unit.x * arrow_size / 2.0f);

					draw->AddTriangleFilled(ImVec2(tip->x + unit.x * arrow_size / 2.0f, tip->y + unit.y * arrow_size / 2.0f), ImVec2(base.x + side.x, base.y + side.y), ImVec2(base.x - side.x, base.y - side.y), colour);
				};
			};

			draw->AddText(ImVec2(tip->x + arrow_size, tip->y - arrow_size), colour, axis_names[axis]);
		};

		const ImU32 free = lit == Handle{ m_tool, std::nullopt } ? lit_colour : free_colour;

		if (m_tool == Tool::Rotate)
			draw->AddCircle(*middle, handle_length * view_ring_share, free, ring_points, handle_width);
		else draw->AddRect(ImVec2(middle->x - centre_size, middle->y - centre_size), ImVec2(middle->x + centre_size, middle->y + centre_size), free, 0.0f, ImDrawFlags_None, handle_width);
	};

	bool ModelEditor::Shaping(const Document& document)
	{
		return document.pick != Pick::Joints && !document.parts.empty();
	};

	std::vector<ModelEditor::JointPose> ModelEditor::Poses(const Document& document)
	{
		std::vector<JointPose> poses{ };

		for (const Joint& joint : document.joints)
		{
			const Transform& transform = joint.object->transform;
			poses.push_back(JointPose{ *transform.position, *transform.rotation, *transform.scale });
		};

		return poses;
	};

	void ModelEditor::SetPoses(Document& document, const std::vector<JointPose>& poses)
	{
		for (size_t i = 0; i < poses.size() && i < document.joints.size(); i++)
		{
			Transform& transform = document.joints[i].object->transform;

			if (JointPose{ *std::as_const(transform.position), *std::as_const(transform.rotation), *std::as_const(transform.scale) } == poses[i])
				continue;

			transform.position = poses[i].position;
			transform.rotation = poses[i].rotation;
			transform.scale = poses[i].scale;
		};
	};

	void ModelEditor::TransformJoints(Document& document, const glm::mat4& change)
	{
		std::set<const GameObject*> picked{ };

		for (const uint32_t joint : document.selection.joints)
			picked.insert(document.joints[joint].object);

		// A move keeps each joint's turn and size as they were, rather than
		// as they come back out of its matrix.
		const bool moving = glm::mat3(change) == glm::mat3(1.0f);

		const auto kept = [](const glm::vec3& from, const glm::vec3& to, float tolerance)
		{
			return glm::all(glm::lessThan(glm::abs(to - from), glm::vec3(tolerance))) ? from : to;
		};

		for (const uint32_t index : document.selection.joints)
		{
			GameObject* joint = document.joints[index].object;
			const GameObject* parent = joint->GetParent();
			bool under = false;

			for (const GameObject* above = parent; above && !under; above = above->GetParent())
				under = picked.contains(above);

			// Moved already, by the joint above it.
			if (under)
				continue;

			const glm::mat4 local = glm::inverse(parent ? parent->WorldMatrix() : glm::mat4(1.0f)) * change * joint->WorldMatrix();

			joint->transform.position = kept(*std::as_const(joint->transform.position), glm::vec3(local[3]), pose_tolerance);

			if (moving)
				continue;

			const glm::vec3 scale(glm::length(glm::vec3(local[0])), glm::length(glm::vec3(local[1])), glm::length(glm::vec3(local[2])));
			const glm::mat3 turn(glm::vec3(local[0]) / scale.x, glm::vec3(local[1]) / scale.y, glm::vec3(local[2]) / scale.z);

			joint->transform.rotation = kept(*std::as_const(joint->transform.rotation), Transform::EulerDegrees(glm::quat_cast(turn)), turn_tolerance);
			joint->transform.scale = kept(*std::as_const(joint->transform.scale), scale, pose_tolerance);
		};
	};

	ModelEditor::Snapshot ModelEditor::Take(const Document& document)
	{
		const EditableMesh mesh = document.parts.empty() ? EditableMesh{ } : document.parts[document.part].editing;

		return Snapshot{ document.part, mesh, document.selection, Poses(document) };
	};

	bool ModelEditor::Differs(const Document& document, const Snapshot& snapshot)
	{
		return Poses(document) != snapshot.poses || (!document.parts.empty() && document.parts[snapshot.part].editing != snapshot.mesh);
	};

	void ModelEditor::Restore(Document& document, const Snapshot& snapshot)
	{
		document.part = snapshot.part;
		document.selection = snapshot.selection;
		SetPoses(document, snapshot.poses);
		Posed(document);

		// A step that only posed joints leaves the mesh, and its bindings, as they are.
		if (document.parts.empty() || document.parts[snapshot.part].editing == snapshot.mesh)
			return;

		document.parts[snapshot.part].editing = snapshot.mesh;
		Changed(document);
	};

	void ModelEditor::Commit(Document& document, Snapshot snapshot)
	{
		document.undo.push_back(std::move(snapshot));

		if (document.undo.size() > max_steps)
			document.undo.erase(document.undo.begin());

		document.redo.clear();
	};

	void ModelEditor::Remember(Document& document)
	{
		Commit(document, Take(document));
	};

	void ModelEditor::Step(Document& document, bool undo)
	{
		std::vector<Snapshot>& from = undo ? document.undo : document.redo;
		std::vector<Snapshot>& to = undo ? document.redo : document.undo;

		if (from.empty())
			return;

		const Snapshot step = std::move(from.back());
		from.pop_back();

		document.part = step.part;
		to.push_back(Take(document));

		Restore(document, step);
	};

	void ModelEditor::Changed(Document& document)
	{
		Part& part = document.parts[document.part];

		part.editing.WriteTo(*part.mesh);

		if (part.skin)
			part.skin->SetBindings(part.editing.ToBindings());

		Bend(part);
		document.unsaved = true;

		// What was under the mouse may be gone, and is found again once it moves.
		document.hoveredVertex.reset();
		document.hoveredEdge.reset();
		document.hoveredFace.reset();
		part.edges.reset();
		document.pickedOf.reset();
	};

	void ModelEditor::Posed(Document& document)
	{
		document.unsaved = true;
		document.hoveredJoint.reset();
	};

	const std::vector<EditableMesh::Edge>& ModelEditor::Edges(Part& part)
	{
		if (!part.edges)
			part.edges = part.editing.Edges();

		return *part.edges;
	};

	const std::vector<glm::vec3>& ModelEditor::Shown(const Part& part)
	{
		return part.skin ? part.shown : part.editing.vertices;
	};

	void ModelEditor::Bend(Part& part)
	{
		if (part.skin == nullptr)
			return;

		const std::vector<glm::vec3>& vertices = part.editing.vertices;

		part.bends = part.skin->Bends(*part.mesh, part.editing.bindings);
		part.bends.resize(vertices.size(), glm::mat4(1.0f));
		part.shown.resize(vertices.size());

		for (size_t v = 0; v < vertices.size(); v++)
			part.shown[v] = glm::vec3(part.bends[v] * glm::vec4(vertices[v], 1.0f));
	};

	void ModelEditor::Reshape(Part& part, const std::set<uint32_t>& vertices, const glm::mat4& change)
	{
		if (part.skin == nullptr)
		{
			part.editing.Transform(vertices, change);
			return;
		};

		std::map<uint32_t, glm::vec3> places{ };

		for (const uint32_t vertex : vertices)
			places[vertex] = glm::vec3(glm::inverse(part.bends[vertex]) * change * part.bends[vertex] * glm::vec4(part.editing.vertices[vertex], 1.0f));

		part.editing.Place(places);
	};

	ModelEditor::Selection ModelEditor::Converted(const EditableMesh& mesh, const Selection& selection, Pick from, Pick to)
	{
		if (from == to)
			return selection;

		const std::set<uint32_t> vertices =
			from == Pick::Vertices ? selection.vertices :
			from == Pick::Edges ? mesh.VerticesOfEdges(selection.edges) :
			mesh.VerticesOfFaces(selection.faces);

		Selection converted{ };
		converted.joints = selection.joints;

		switch (to)
		{
		case Pick::Vertices:	converted.vertices = vertices; break;
		case Pick::Edges:		converted.edges = mesh.EdgesWithin(vertices); break;
		default:				converted.faces = mesh.FacesWithin(vertices); break;
		};

		return converted;
	};

	void ModelEditor::SetPick(Pick pick)
	{
		Document* document = Front();

		if (document == nullptr || (pick == Pick::Joints ? document->joints.empty() : document->parts.empty()))
			return;

		// The joints picked and the part of the mesh picked are kept apart.
		if (pick != Pick::Joints && document->pick != Pick::Joints)
			document->selection = Converted(document->parts[document->part].editing, document->selection, document->pick, pick);

		document->pick = pick;
	};

	void ModelEditor::SelectAll()
	{
		Document* document = Front();

		if (document == nullptr)
			return;

		if (document->pick == Pick::Joints)
		{
			for (uint32_t joint = 0; joint < document->joints.size(); joint++)
				document->selection.joints.insert(joint);

			return;
		};

		if (document->parts.empty())
			return;

		const EditableMesh& mesh = document->parts[document->part].editing;
		std::set<uint32_t> vertices{ };

		for (uint32_t vertex = 0; vertex < mesh.vertices.size(); vertex++)
			vertices.insert(vertex);

		document->selection = Converted(mesh, Selection{ vertices }, Pick::Vertices, document->pick);
	};

	void ModelEditor::SelectNone()
	{
		if (Document* document = Front())
			document->selection = { };
	};

	void ModelEditor::Extrude()
	{
		Document* document = Front();

		if (document == nullptr || !Shaping(*document))
			return;

		const std::set<uint32_t> faces = PickedFaces(*document);
		EditableMesh& mesh = document->parts[document->part].editing;

		if (faces.empty())
		{
			document->message = "Pick the faces to extrude first.";
			document->messageTime = ImGui::GetTime();
			return;
		};

		float edges = 0.0f;
		int count = 0;

		for (const uint32_t face : faces)
		{
			const std::vector<EditableMesh::Corner>& corners = mesh.faces[face].corners;

			for (size_t i = 0; i < corners.size(); i++, count++)
				edges += glm::distance(mesh.vertices[corners[i].vertex], mesh.vertices[corners[(i + 1) % corners.size()].vertex]);
		};

		Snapshot before = Take(*document);

		if (!mesh.Extrude(faces, edges / count * extrude_share))
		{
			document->message = "These faces face every way between them, so there is no one way to pull them out. Pick fewer.";
			document->messageTime = ImGui::GetTime();
			return;
		};

		Commit(*document, std::move(before));

		document->pick = Pick::Faces;
		document->selection = Selection{ { }, { }, faces };
		m_tool = Tool::Move;

		Changed(*document);

		document->message = "Extruded " + std::to_string(faces.size()) + " " + PickName(Pick::Faces, faces.size()) + ". Drag the arrows to pull them further.";
		document->messageTime = ImGui::GetTime();
	};

	void ModelEditor::Duplicate()
	{
		Document* document = Front();

		if (document == nullptr || !Shaping(*document))
			return;

		const std::set<uint32_t> faces = PickedFaces(*document);

		if (faces.empty())
		{
			document->message = "Pick the faces to duplicate first.";
			document->messageTime = ImGui::GetTime();
			return;
		};

		Remember(*document);

		document->pick = Pick::Faces;
		document->selection = Selection{ { }, { }, document->parts[document->part].editing.Duplicate(faces) };
		m_tool = Tool::Move;

		Changed(*document);

		document->message = "Copied " + std::to_string(faces.size()) + " " + PickName(Pick::Faces, faces.size()) + ". Drag the arrows to move the copy.";
		document->messageTime = ImGui::GetTime();
	};

	void ModelEditor::Delete()
	{
		Document* document = Front();

		if (document == nullptr || !Shaping(*document) || Moving(*document).empty())
			return;

		Remember(*document);

		EditableMesh& mesh = document->parts[document->part].editing;

		switch (document->pick)
		{
		case Pick::Vertices:	mesh.DeleteVertices(document->selection.vertices); break;
		case Pick::Edges:		mesh.DeleteEdges(document->selection.edges); break;
		default:				mesh.DeleteFaces(document->selection.faces); break;
		};

		document->selection = { };

		Changed(*document);
	};

	void ModelEditor::Flip()
	{
		Document* document = Front();

		if (document == nullptr || !Shaping(*document))
			return;

		const std::set<uint32_t> faces = PickedFaces(*document);

		if (faces.empty())
			return;

		Remember(*document);
		document->parts[document->part].editing.Flip(faces);

		Changed(*document);
	};

	void ModelEditor::Add(Shapes::Shape shape)
	{
		Document* document = Front();

		if (document == nullptr || document->parts.empty())
			return;

		EditableMesh& mesh = document->parts[document->part].editing;
		const auto [low, high] = mesh.Bounds();
		const glm::vec3 extent = high - low;
		const float largest = (std::max)({ extent.x, extent.y, extent.z });
		const float size = largest > 0.0f ? largest * add_share : empty_size;

		EditableMesh added = Shapes::Build(shape, size);

		std::set<uint32_t> all{ };

		for (uint32_t vertex = 0; vertex < added.vertices.size(); vertex++)
			all.insert(vertex);

		// Where the view is looking, in the part's own space.
		const glm::mat4 local = glm::inverse(document->parts[document->part].object->WorldMatrix());
		added.Transform(all, glm::translate(glm::mat4(1.0f), glm::vec3(local * glm::vec4(document->target, 1.0f))));

		Remember(*document);

		document->pick = Pick::Faces;
		document->selection = Selection{ { }, { }, mesh.Append(added) };
		m_tool = Tool::Move;

		Changed(*document);

		document->message = std::string("Added a ") + Shapes::Label(shape) + " where the view is looking. It is picked, so the arrows move it; tick See Through if it is inside the model.";
		document->messageTime = ImGui::GetTime();
	};

	GameObject* ModelEditor::Model() const
	{
		const Document* document = Front();

		return document ? document->root : nullptr;
	};

	std::vector<GameObject*> ModelEditor::Joints() const
	{
		std::vector<GameObject*> joints{ };

		if (const Document* document = Front())
			for (const Joint& joint : document->joints)
				joints.push_back(joint.object);

		return joints;
	};

	void ModelEditor::Undo()
	{
		if (Document* document = Front())
			Step(*document, true);
	};

	void ModelEditor::Redo()
	{
		if (Document* document = Front())
			Step(*document, false);
	};

	void ModelEditor::AddMesh()
	{
		Document* document = Front();

		// A mesh the top object has already, which cannot be shaped here, is
		// left alone rather than joined by a second.
		if (document == nullptr || document->root == nullptr || !document->parts.empty() || document->root->GetComponent<Mesh>())
			return;

		if (document->root->GetComponent<Material>() == nullptr)
		{
			const std::string shader = ShaderOf(*document->root);
			document->root->Attach<Material>()->SetShaderPath(shader.empty() ? ProjectAssets::ProjectShader(ProjectFolder(document->path).string()) : shader);
		};

		Mesh* mesh = document->root->Attach<Mesh>(Mesh::Triangles);
		document->parts.push_back(Part{ document->root, mesh, EditableMesh{ } });

		Add(Shapes::Shape::Cube);
	};

	std::vector<std::string> ModelEditor::Shaders() const
	{
		const Document* document = Front();

		return document ? ProjectShaders(document->path) : std::vector<std::string>{ };
	};

	void ModelEditor::SetShader(const std::string& path)
	{
		if (Document* document = Front(); document && document->root)
			SetShader(*document, path);
	};

	void ModelEditor::SetShader(Document& document, const std::string& path)
	{
		std::vector<Material*> materials{ };
		MaterialsOf(*document.root, materials);

		for (Material* material : materials)
			if (material->GetShaderPath() != path)
			{
				material->ChangeShader(path);
				document.unsaved = true;
			};
	};

	bool ModelEditor::Save()
	{
		Document* document = Front();

		return document && SaveDocument(*document);
	};
};
