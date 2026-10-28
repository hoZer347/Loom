#pragma once

#include "EditableMesh.h"
#include "EditorViewport.h"
#include "Shapes.h"
#include "UndoHistory.h"

#include "Camera.h"
#include "Guid.h"

#include "imgui.h"

#include "glm/glm.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>


namespace Loom
{
	struct GameObject;
	struct Mesh;
	struct Scene;

	/**
	* Loom::ModelEditor
	* - The Model panel: a tab for each prefab open in it, whose meshes are
	*   edited a vertex, edge or face at a time
	* - The left mouse picks what is under it, or what a dragged box takes in,
	*   and drags the handles that move, turn and size it. The right mouse
	*   orbits, the middle pans and the wheel zooms
	* - Every action has a button, named and greyed out until it can be used,
	*   and the line along the bottom says what the mouse would do. Ctrl+Z and
	*   Ctrl+Y step back and forward an action at a time
	* - Ctrl+S writes the meshes back into the prefab, and every instance of
	*   it picks the change up
	*/
	struct ModelEditor final
	{
		#pragma region Types

		// What a click picks.
		enum struct Pick
		{
			Vertices,
			Edges,
			Faces,
		};

		// What the handles do to what is picked.
		enum struct Tool
		{
			Move,
			Rotate,
			Scale,
		};

		struct Selection
		{
			std::set<uint32_t> vertices;
			std::set<EditableMesh::Edge> edges;
			std::set<uint32_t> faces;

			bool operator==(const Selection&) const = default;
		};

		#pragma endregion

		#pragma region Lifetime

		ModelEditor() = default;
		~ModelEditor();

		ModelEditor(const ModelEditor&) = delete;
		ModelEditor& operator=(const ModelEditor&) = delete;

		#pragma endregion

		#pragma region Opening

		// Opens the prefab in a tab of its own, or brings its tab forward, and
		// the panel with it.
		void Open(const std::string& path);

		// Opens text, a prefab not yet written, in a tab that writes it to path
		// when saved, or brings path's tab forward. The file at path is read
		// instead once there is one.
		void Open(const std::string& path, const std::string& text);

		// Whether a tab has the prefab at path open.
		bool IsOpen(const std::string& path) const;

		#pragma endregion

		#pragma region Documents

		// Keeps the tabs of prefabs that were moved saving to where they are now.
		void Relocate(const std::string& from, const std::string& to);

		// Every tab with unsaved changes, to its prefab. False when one could
		// not be written.
		bool SaveAll();

		// The names of the prefabs with unsaved changes.
		std::vector<std::string> UnsavedFiles() const;

		// The prefabs saved since the last call.
		std::vector<std::string> TakeSaved();

		bool Save();

		#pragma endregion

		#pragma region Accessors

		// The tab in front's mesh being edited, null with none open or none in
		// its prefab.
		const EditableMesh* Editing() const;
		const Selection* Selected() const;

		// Whether the tab in front differs from its prefab as last read or saved.
		bool Unsaved() const;

		// Where a point of the mesh being edited was drawn in the panel when
		// it last drew, in screen pixels. None behind the camera.
		std::optional<ImVec2> ScreenOf(const glm::vec3& point) const;

		#pragma endregion

		#pragma region Window

		void Draw(bool* open);

		// Whether the panel had the keyboard when it was last drawn, so the
		// scene's own undo keeps out of its way.
		bool IsFocused() const { return m_focused; };

		#pragma endregion

		#pragma region Undo

		void Undo();
		void Redo();

		#pragma endregion

		#pragma region Selection

		// What the toolbar's buttons do, to the tab in front. Each change is one
		// step for undo.
		void SetPick(Pick pick);
		void SetTool(Tool tool) { m_tool = tool; };
		void SelectAll();
		void SelectNone();

		#pragma endregion

		#pragma region Edits

		void Extrude();
		void Duplicate();
		void Delete();
		void Flip();
		void Add(Shapes::Shape shape);

		#pragma endregion

		#pragma region New Mesh

		// Gives a prefab with no mesh one on its top object, starting as a cube.
		void AddMesh();

		#pragma endregion

		#pragma region Shaders

		// The shaders the front tab's project has, relative to it, and which
		// one every material of its prefab draws with.
		std::vector<std::string> Shaders() const;
		void SetShader(const std::string& path);

		#pragma endregion

	private:
		#pragma region Types

		// A GameObject of the prefab with a mesh, and that mesh as it is edited.
		struct Part
		{
			GameObject* object = nullptr;
			Mesh* mesh = nullptr;
			EditableMesh editing;
		};

		struct Snapshot
		{
			size_t part = 0;
			EditableMesh mesh;
			Selection selection;

			// Each material's shader, by the material's guid.
			std::vector<std::pair<Guid, std::string>> shaders;

			// False for the prefab as it was before Add a Mesh gave it one.
			bool meshed = true;
		};

		// One of the handles round what is picked, along an axis or, with
		// none, across the screen.
		struct Handle
		{
			Tool tool = Tool::Move;
			std::optional<int> axis;

			bool operator==(const Handle&) const = default;
		};

		enum struct Drag
		{
			None,
			Click,
			Box,
			Handle,
			Orbit,
			Pan,
		};

		// How the canvas met the world when it last drew.
		struct View
		{
			ImVec2 corner{ };
			ImVec2 size{ };
			glm::mat4 viewProjection{ 1.0f };
			glm::vec3 eye{ 0.0f };
			glm::vec3 right{ 1.0f, 0.0f, 0.0f };
			glm::vec3 up{ 0.0f, 1.0f, 0.0f };

			// The part's own space to the world, and back.
			glm::mat4 world{ 1.0f };
			glm::mat4 local{ 1.0f };

			// World units across a screen pixel, one unit in front of the eye.
			float pixel = 0.0f;
		};

		struct Document
		{
			std::string path;
			Scene* scene = nullptr;
			GameObject* root = nullptr;
			std::vector<Part> parts;
			size_t part = 0;

			Pick pick = Pick::Faces;
			Selection selection;

			UndoHistory<Snapshot> history;
			bool unsaved = false;

			// The Mesh and any Material Add a Mesh put on the top object, which
			// undo takes off and redo puts back as the same components.
			std::optional<Guid> addedMesh;
			std::optional<Guid> addedMaterial;

			// When the file was last read or written, and what it held then, so
			// a change made to it elsewhere is read in again.
			std::filesystem::file_time_type written{ };
			std::string text;

			// What a tab opened before its file was written holds until it is.
			std::string unwritten;

			// The camera turns about target, distance away.
			glm::vec3 target{ 0.0f };
			float distance = 0.0f;
			float yaw = 0.0f;
			float pitch = 0.0f;
			Camera camera;

			EditorViewport viewport{ false };
			View view;

			Drag drag = Drag::None;
			ImVec2 pressed{ };
			Handle held;
			glm::vec3 pivot{ 0.0f };

			// How things stood when a drag that may change the mesh began, a
			// step for undo once it has.
			Snapshot pending;
			bool wasUnsaved = false;

			// The meshes that cannot be shaped here: animated, made by a
			// script, or not triangles.
			size_t skipped = 0;

			// Worked out from the mesh being edited, and from it and what is
			// picked, and kept until those change.
			std::optional<size_t> edgesOf;
			std::vector<EditableMesh::Edge> edges;
			std::optional<std::pair<size_t, Pick>> pickedOf;
			Selection pickedFrom;
			std::set<uint32_t> pickedFaces;

			std::optional<Handle> hoveredHandle;
			std::optional<uint32_t> hoveredVertex;
			std::optional<EditableMesh::Edge> hoveredEdge;
			std::optional<uint32_t> hoveredFace;

			// The part hoveredFace is a face of, which may not be the one edited.
			size_t hoveredPart = 0;

			// What the last action did, shown for a while from when it did it.
			std::string message;
			double messageTime = 0.0;

			// Brings the tab forward on the next frame.
			bool select = true;
		};

		#pragma endregion

		#pragma region Lifetime

		bool Compile();

		#pragma endregion

		#pragma region Documents

		bool Load(Document& document);
		bool SaveDocument(Document& document);
		void Unload(Document& document);

		#pragma endregion

		#pragma region Accessors

		Document* Front() const;

		#pragma endregion

		#pragma region Window

		void DrawDocument(Document& document);
		void HandleKeys(Document& document);

		#pragma endregion

		#pragma region Panels

		void DrawToolbar(Document& document);
		void DrawSide(Document& document);
		void DrawStatus(const Document& document);

		#pragma endregion

		#pragma region Closing

		void DrawCloseQuestion();
		void Close(const Document* document);

		#pragma endregion

		#pragma region Canvas

		void DrawCanvas(Document& document, const ImVec2& size);

		#pragma endregion

		#pragma region Camera

		void Steer(Document& document, bool hovered);
		void Frame(Document& document);
		void Pose(Document& document);
		void Measure(Document& document, const ImVec2& corner, const ImVec2& size);
		std::optional<ImVec2> Project(const View& view, const glm::vec3& world) const;
		void Ray(const View& view, const ImVec2& mouse, glm::vec3& origin, glm::vec3& direction) const;

		#pragma endregion

		#pragma region Hit Testing

		bool Visible(const Document& document, const glm::vec3& local) const;
		std::optional<Handle> HandleAt(const Document& document, const ImVec2& mouse) const;

		// The vertices the selection moves, and the middle of them in the
		// part's own space.
		std::set<uint32_t> Moving(const Document& document) const;
		std::optional<glm::vec3> Pivot(const Document& document) const;

		// The faces an action on faces takes: those picked, or those whose
		// every corner is picked.
		const std::set<uint32_t>& PickedFaces(Document& document) const;

		#pragma endregion

		#pragma region Picking

		void Hover(Document& document, const ImVec2& mouse);
		void Click(Document& document, const ImVec2& mouse);
		void Box(Document& document, const ImVec2& from, const ImVec2& to);

		#pragma endregion

		#pragma region Dragging

		void DragHandle(Document& document, const ImVec2& mouse);

		#pragma endregion

		#pragma region Rendering

		void Render(Document& document);
		void DrawOverlay(Document& document);
		void DrawHandles(const Document& document, ImDrawList* draw) const;

		#pragma endregion

		#pragma region Undo

		static Snapshot Take(const Document& document);
		static void Commit(Document& document, Snapshot snapshot);
		static void Remember(Document& document);
		static void Step(Document& document, bool undo);

		// Off the top object, and back on, what Add a Mesh put there.
		static void Unmesh(Document& document);
		static void Remesh(Document& document);

		// Add, as one step with before as it stood ahead of it.
		void AddShape(Shapes::Shape shape, std::optional<Snapshot> before);

		#pragma endregion

		#pragma region Changes

		static void Changed(Document& document);
		static const std::vector<EditableMesh::Edge>& Edges(Document& document);

		#pragma endregion

		#pragma region Selection

		static Selection Converted(const EditableMesh& mesh, const Selection& selection, Pick from, Pick to);

		#pragma endregion

		#pragma region Shaders

		static void SetShader(Document& document, const std::string& path);

		#pragma endregion

		std::vector<std::unique_ptr<Document>> m_documents;
		Document* m_front = nullptr;

		// Closed with edits that were never saved, until the user says what to
		// do with them.
		Document* m_closing = nullptr;

		std::vector<std::string> m_saved;

		// What the shader menu lists, found again each time it opens.
		std::vector<std::string> m_shaders;

		Tool m_tool = Tool::Move;
		bool m_seeThrough = false;
		bool m_focused = false;

		// Frames left to ask for focus on.
		int m_focusFrames = 0;

		uint32_t m_program = 0;
		bool m_compileTried = false;
	};
};
