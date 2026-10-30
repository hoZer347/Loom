#pragma once

#include "AssetKind.h"
#include "AssetTrash.h"
#include "ColliderGizmos.h"
#include "ComponentMenu.h"
#include "DialogueEditor.h"
#include "EditorCamera.h"
#include "EditorConsole.h"
#include "EditHistory.h"
#include "KeyBindings.h"
#include "EditorLayouts.h"
#include "EditorRefresh.h"
#include "EditorUpdate.h"
#include "EditorViewport.h"
#include "FolderWatch.h"
#include "KindFilter.h"
#include "LoadProgress.h"
#include "GameFocus.h"
#include "Guid.h"
#include "Hierarchy.h"
#include "EditorSettings.h"
#include "ShaderEditor.h"
#include "TestsPanel.h"
#include "ExtensionLibrary.h"
#include "Shapes.h"
#include "BuildSettings.h"
#include "GameBuilder.h"
#include "SpriteEditor.h"
#include "ModelEditor.h"
#include "ModelPreview.h"
#include "Prefabs.h"
#include "SelectionOutline.h"
#include "SceneHistory.h"
#include "SceneSerializer.h"
#include "SearchBar.h"
#include "PlayHistory.h"
#include "FileTab.h"
#include "FolderTree.h"
#include "ScriptFolders.h"
#include "ScriptLibrary.h"
#include "TabHistory.h"
#include "TornWindows.h"
#include "WebPlayer.h"
#include "WindowFrame.h"
#include "WindowFullscreen.h"

#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct ImGuiContext;
struct ImGuiWindow;
struct ImRect;
struct ImGuiMultiSelectIO;


namespace Loom
{
	struct Scene;
	struct GameObject;
	struct ComponentBase;
	struct LoomObject;
	struct Texture;

	// The file dialogs, kept behind a pointer so Editor.h does not have to drag
	// the file browser (and <filesystem>) into everything that includes it.
	struct EditorFileDialogs;

	// A file or folder under the project, as the Project panel lists it.
	struct AssetNode
	{
		std::string path;
		std::string name;
		bool folder = false;
		std::vector<AssetNode> children{ };

		// A file's, as the scan found it.
		const char* icon = nullptr;
	};

	// What the Scripts tab lists.
	enum class ScriptEntryKind
	{
		Script,
		State,
		Shader,
		PixelShader,
		ComputeShader,
		Dialogue,
		PreVertex,
	};

	/**
	* Loom::Editor
	* - A Unity-style shell around the Loom runtime
	* - Owns one dock space holding the scene hierarchy, an inspector, a console
	*   and, in the centre, two views rendered into off-screen targets so they
	*   sit inside the layout like any other panel: the game as its own cameras
	*   see it, and everything loaded, from a camera the editor flies
	* - Works out of a project folder, which the Project panel lists in tabs:
	*   its scenes and prefabs in the Scenes tab, which also creates them, its
	*   scripts and shaders in the Scripts tab, which creates those, and its
	*   textures, models, sounds and fonts each in a FileTab, the 2D tab
	*   creating textures. The scripts are a C++ project that builds into a
	*   library the editor loads
	* - Reads and writes scenes through SceneSerializer, and shows the text form
	*   of the open scene live in its Scene Data panel
	* - Takes over Engine's GUI hook on construction and hands it back on
	*   destruction, so nothing else has to know the editor exists
	*/
	struct Editor final
	{
		#pragma region Construction

		Editor();
		~Editor();

		Editor(const Editor&) = delete;
		Editor& operator=(const Editor&) = delete;

		#pragma endregion

		#pragma region Scenes

		// Scenes created or loaded here are owned by the editor and can be
		// closed from the Hierarchy; scenes created anywhere else still show up
		// in it, they just cannot be closed from it.
		Scene* CreateScene(const std::string& name = "New Scene");
		Scene* OpenScene(const std::string& path);
		void CloseScene(Scene* scene);

		// What the Hierarchy drags: a GameObject's guid rather than its address,
		// so a drop resolves against what still exists when the mouse comes up.
		static constexpr const char* object_payload = "LoomObject";

		// What double-clicking a scene in the Project panel does: every open
		// scene is closed, so this one is all there is, once the question
		// about any unsaved is answered. Stops a run first. The scene is read
		// over the frames that follow, with a bar in the views; false when it
		// cannot start.
		bool OpenSceneAlone(const std::string& path);

		void SetActiveScene(Scene* scene);

		bool IsOwned(Scene* scene) const;

		// The file an owned scene was opened from or saved to, empty for one
		// never saved.
		std::string ScenePathOf(Scene* scene) const;

		// Whether an owned scene differs from its file, or has none.
		bool SceneUnsaved(Scene* scene) const;

		#pragma endregion

		#pragma region Prefabs

		// Writes the GameObject to a new .loomprefab in folder, named after it,
		// and makes it an instance of that prefab. The path of the file, or
		// empty when it could not be written.
		std::string SaveAsPrefab(GameObject* gameObject, const std::string& folder);

		// Takes the link off a prefab instance, leaving its content as plain
		// GameObjects the prefab no longer keeps alike.
		void UnpackPrefab(GameObject& gameObject);

		// A new instance of the prefab at path, from the project folder, as
		// the last child of parent.
		GameObject* Instantiate(const std::string& path, GameObject& parent);

		// Shows the prefab in the Prefab panel, in a scene of its own that
		// only the panel draws, with a light the Hierarchy leaves out. With
		// edits held in the panel, another prefab waits for the question
		// about them, and the same one keeps them.
		void OpenPrefab(const std::string& path);
		void ClosePrefab();

		// Writes the Prefab panel's held edits to the file, and from there to
		// every instance. Whether nothing is left unsaved.
		bool SavePrefab();
		GameObject* GetPrefabRoot() const;

		#pragma endregion

		#pragma region Saving

		bool SaveScene(Scene* scene, const std::string& path);

		// File > Save Scene and Ctrl+S: the active scene, to the file it was
		// opened from, or, for one never saved, a new file named after it in
		// the project's scenes folder. Refused while playing, when the scene
		// holds what the run has done to it.
		void SaveActiveScene();

		// File > Exit, the window's close button and Alt+F4: stops any run,
		// then closes once the question about anything unsaved is answered.
		void Quit();

		#pragma endregion

		#pragma region Refresh

		// File > Full Refresh and Ctrl+Shift+R: rebuilds the editor from the
		// engine's sources, then stops any run, saves the project and closes,
		// for main to start the new build on it. A failed build leaves this
		// editor running.
		void FullRefresh();
		bool IsRefreshing() const { return m_refresh.IsRunning() || m_relaunch; };

		// Asks for what a merge calls for, for ApplyUpdate to run when it can.
		void RequestUpdate(EditorUpdate::Kind kind) { m_update.Request(kind); };
		EditorUpdate::Kind UpdateWaiting() const { return m_update.Waiting(); };

		// Whether the editor closed to be started again, from which executable
		// and on what: the scene that was active, or the project when none was.
		bool WantsRelaunch() const { return m_relaunch; };
		const std::filesystem::path& RelaunchExecutable() const { return m_refresh.GetExecutable(); };
		std::string RelaunchTarget() const;

		#pragma endregion

		#pragma region Project

		// The folder the editor works out of. Everything under it is listed in
		// the Project panel; the choice is remembered between runs, so the
		// folder prompt only shows up when there is nothing to remember.
		// A .loomproject file opens the folder it is in, after giving it a
		// scripts project if it has none, and a .loomscene file opens its
		// project on that scene.
		bool OpenProject(const std::string& path, bool open_scene = true);

		// Opens one of the projects in <engine>/Demos, by its folder name.
		bool OpenDemo(const std::string& name);

		// Writes a new project (scenes, scripts, a Visual Studio solution) into
		// folder/name and opens it.
		bool CreateProject(const std::string& folder, const std::string& name);

		// Builds the project's script library and loads the result. The open
		// scenes go out and come back around it, because their components are
		// code that is about to be replaced.
		void CompileScripts();

		#pragma endregion

		#pragma region Asset Editors

		// In a tab of the Shaders panel, which comes forward.
		void OpenShader(const std::string& path);

		// In a tab of the Dialogues panel, which comes forward.
		void OpenDialogue(const std::string& path);

		// In a tab of the Sprites panel, which comes forward. A preview takes
		// the place of the one before it.
		void OpenSprite(const std::string& path, bool preview = false);

		// A prefab, by its path in the project or on disk, in a tab of the
		// Model panel, which comes forward. A model opens as the prefab named
		// after it beside it, imported afresh until that is saved.
		void OpenModel(const std::string& path);

		// In the Palette panel, which comes forward, for the sprite in front.
		void OpenPalette(const std::string& path);

		#pragma endregion

		#pragma region Layouts

		// A layout from the Layout menu: loaded at the top of the next frame,
		// saved once the panels have been drawn, or asked a name for.
		void LoadLayout(const std::string& name) { m_pendingLayout = name; };
		void SaveLayout(const std::string& name) { m_layoutToSave = name; };
		void ShowSaveLayoutPrompt();

		#pragma endregion

		#pragma region Inspector

		// The Inspector's Add Component menu, opened on the selection the next
		// time it is drawn.
		void ShowAddComponent() { m_openAddComponent = true; };

		// Files a component under category in the open project, or back under
		// its own when category is empty.
		void SetComponentCategory(const std::string& component, const std::string& category);

		#pragma endregion

		#pragma region Play

		// Starts or pauses the scenes updating. Starting compiles first when
		// the scripts have moved on since the library was built.
		void TogglePlay();

		// Leaves play mode: updating goes off and the scenes the editor owns go
		// back to the state they were in when Play was first pressed. Mid-build
		// there are no scenes to put back yet, so the reset lands with the build.
		void Stop();

		// Whether there is a play session to stop, running or paused.
		bool IsPlaying() const;

		// Builds the project for the web and opens the active scene in the
		// default browser, as it was when Play was pressed if it is running.
		void PlayWeb();

		#pragma endregion

		#pragma region Screenshots

		// Writes the editor as it stands - panels and all - to a bitmap once the
		// given number of frames have gone by, so a scene can be looked at
		// without a visible window. Given a window, only the first shown one
		// whose name contains it.
		void RequestScreenshot(const std::string& path, int after_frames, bool exit_after, const std::string& window = "");

		#pragma endregion

		#pragma region Queued Input

		// Imports a model file once the next frame has drawn the panels, into
		// whatever the mouse is over: a Hierarchy node, one of the views, or
		// failing those the selection.
		void QueueImport(const std::string& path);

		// The same, with the mouse moved to x, y first, in ImGui's coordinates.
		// Over the Scenes tab or a FileTab, the file is copied into the
		// folder there instead.
		void Drop(const std::string& path, float x, float y);

		// An undo or redo to run once the queued imports have landed and been
		// recorded, one per frame, so an automated run can step back over them.
		void QueueHistoryStep(bool undo);

		// A left click at x, y in ImGui's coordinates, let go at to_x, to_y,
		// once the scripts are built, so an automated run can click through
		// the panels without the real mouse.
		void QueueClick(float x, float y, float to_x, float to_y);

		// Runs the tests whose names hold filter, all of them for an empty one.
		void QueueTestRun(const std::string& filter);

		// False in a run that exits once its screenshot or UI script is done,
		// which nobody is at to see Visual Studio come up.
		bool opensVisualStudio = true;

		#pragma endregion

		#pragma region Dialogs

		// File > Open Project..., starting beside the project that is open.
		void ShowOpenProjectDialog();

		// Window > Settings.
		void ShowSettings() { m_showSettings = true; };

		#pragma endregion

		#pragma region Selection

		// Deselects any GameObject; the Inspector shows the model instead.
		void SelectModel(const std::string& path);

		// Clears the selected model too.
		void Select(GameObject* gameObject);

		// Selects objects, the Inspector drawn from the first. A scene's root is
		// only ever selected alone, so one among others is left out.
		void SetSelection(const std::vector<GameObject*>& objects);

		// Every GameObject selected, the one the Inspector is drawn from first.
		std::vector<GameObject*> GetSelection() const;

		// Selects the GameObject with this name once its scene has loaded, so a
		// screenshot can show it in the inspector. Each further request before
		// then adds another to the selection.
		void RequestSelection(const std::string& name);

		#pragma endregion

		#pragma region History

		// Steps the owned scenes back or forward through the edits made to them.
		// While playing, only through the edits made in the Inspector since
		// Play: what a run changes is not an edit.
		void Undo();
		void Redo();

		#pragma endregion

		#pragma region Settings

		// The graphics API the settings file says to start on, which main reads
		// before the Engine opens its window. OpenGL when nothing is saved.
		static Backend SavedBackend();

		// Prints each scripts project it opens, for a launcher that checks which
		// one a run built. Set before construction, which opens the last project.
		static inline bool logScriptsProject = false;

		// Where extensions load from, for a run that must not use the user's.
		// Set before construction.
		static inline std::filesystem::path extensionsFolder{ };

		#pragma endregion

		#pragma region Editing

		// The selection as the text a scene writes it as, on the system
		// clipboard, so it can be pasted into another scene or another editor.
		// A paste lands beside the selection.
		void Cut();
		void Copy();
		void Paste();

		void DeleteSelected();

		#pragma endregion

		#pragma region Asset Creation

		// Create > Script, State or State Machine in the project folder, named
		// and confirmed.
		void CreateScriptAsset(AssetKind kind, const std::string& name);

		// Opens the Create prompt for that kind of asset, named the first default
		// name nothing is using.
		void AskForAsset(AssetKind kind, const std::string& folder);

		#pragma endregion

		#pragma region Access

		const std::string& GetProjectPath() const { return m_projectPath; };

		// What EditorMcp reads the editor through.
		Scene* GetActiveScene() const { return m_activeScene; };
		GameObject* GetSelected() const { return m_selected; };
		const ScriptLibrary& GetScripts() const { return m_scripts; };

		// An edit made other than through the panels, recorded for undo like
		// theirs once nothing is held down.
		void MarkEdited() { m_historyDirty = true; };

		#pragma endregion

	private:
		#pragma region Project Panel

		// The Project panel, its tabs drawn by the ones below.
		void DrawProject();

		// Brings the Project panel forward with one of ProjectTabs in front.
		void ShowProjectTab(std::string_view tab);

		// The file or folder picked in the Project panel's tab in front, or none.
		std::string SelectedProjectEntry() const;

		// Takes it out of the project, a step Ctrl+Z puts back from the panel.
		void DeleteSelectedEntry();

		// Whether the paths can go: none holds the project or an open scene, and
		// no tab on them has unsaved changes, or the console says which.
		bool CanDeleteEntries(const std::vector<std::string>& paths) const;

		// Closes what was showing the paths, now they have gone.
		void ForgetEntries(const std::vector<std::string>& paths);

		// Whether Ctrl+Z or Ctrl+Y is the Project panel's, and the step itself.
		bool ProjectTakesStep(bool undo) const;
		void StepEntries(bool undo);

		// The project's scenes and prefabs, in the folders they are in.
		void DrawScenes();
		// found: a folder above matched the search, so all of this one shows.
		void DrawAssetNode(const AssetNode& node, int depth, bool found);
		bool ShowsAsset(const AssetNode& node) const;
		void DrawAssetContextMenu(const AssetNode& node);
		void DrawCreateMenu(const std::string& folder);
		void DrawCreateAssetPrompt();

		// The project's script types and shaders, filed in folders of the
		// Scripts tab's own that move no files.
		void DrawScripts();
		void DrawScriptEntry(const std::string& name);
		void DrawScriptsCreateMenu(const std::string& folder);

		// The Scripts tab's folders around names, the scripts in each in the
		// order of what the tab calls them.
		VirtualFolders::Node ScriptsByLabel(const std::vector<std::string>& names) const;

		// How the Scripts tab's FolderTree draws and files its scripts.
		FolderTree::Hooks ScriptFolderHooks();

		// Where the project keeps them: beside its .loomproject, named the same.
		// Empty for a folder with no project file.
		std::string ScriptFoldersFile() const;
		void SaveScriptFolders();

		// The FileTabs, each drawn alike.
		void DrawFileTab(FileTab& tab);
		void DrawFileEntry(FileTab& tab, const std::string& file);
		void DrawFileCreateMenu(FileTab& tab, const std::string& folder);
		FolderTree::Hooks FileFolderHooks(FileTab& tab);
		std::string FileFoldersFile(const FileTab& tab) const;
		void SaveFileFolders(const FileTab& tab);

		// The FileTab that holds the file, or none.
		FileTab* FileTabOf(const std::string& path);

		// Selects and shows the file at path in its FileTab. False when none
		// holds it.
		bool RevealFile(const std::string& path);

		// Selects, shows and flashes the file at path in the Scenes tab.
		void PingAsset(const std::string& path);

		// Where the project keeps its InputActions, beside its .loomproject in
		// the same way. Empty for a folder with no project file.
		std::string InputActionsFile() const;
		bool SaveInputActions();

		// The Build Settings tab, and the game built from it.
		std::string BuildSettingsFile() const;
		bool SaveBuildSettings();
		std::vector<std::string> BuildScenes() const;
		void DrawBuildSettings();
		void BuildGame(bool run);

		// Rereads the scripts project's headers for the types they declare.
		void RefreshScripts();

		#pragma endregion

		#pragma region Field Pickers

		// A field's value on a button the field's width, which a click opens
		// the field's picker under. shown is cut from the front to fit behind
		// icon, when given. Its tooltip is tip, or shown in full when cut.
		void DrawPickerButton(const std::string& shown, const char* icon = nullptr, const std::string& tip = { });

		// While the picker of the button drawn last is open: its search, and
		// the list the field draws its rows in until EndPicker. appearing, when
		// given: whether it opened this frame.
		bool BeginPicker(bool* appearing = nullptr);
		void EndPicker();

		// One of the open picker's rows, its label fit as the button's is.
		// Whether it was picked, which closes the picker.
		static bool PickerRow(
			const std::string& id,
			const std::string& label,
			bool selected,
			const char* icon = nullptr,
			const std::string& tip = { });

		// An Asset field: the file it names, which a click swaps for another
		// picked from the project's files of the field's extension, filed as
		// the tab holding them files them, shaders as the Scripts tab does,
		// and by their paths where no tab holds them.
		// Takes such a file dragged onto it. Whether it changed.
		bool DrawAssetField(const SerializedField& field);

		// An AssetList field: one Asset field per file, each with a button that
		// takes it out, under the label's column, and a button that adds one.
		bool DrawAssetListField(const SerializedField& field);

		#pragma endregion

		#pragma region Hierarchy

		// The Hierarchy's right-click menu on empty space adds to the rows
		// above it, as a drop there does.
		void DrawHierarchy();
		void DrawSceneNode(Scene* scene);
		// A prefab instance's row shows the prefab icon, and it and everything
		// under it take the accent colour.
		void DrawGameObjectNode(GameObject* gameObject, bool parent_disabled, bool parent_in_prefab);

		// Only the rows in view are drawn, and the rest are stepped over, so the
		// tree costs what is open and on screen rather than what the scenes hold.
		void DrawChildNodes(GameObject& parent, bool disabled, bool in_prefab);

		// The 3D Object submenu, each shape made a child of parent and
		// selected. Disabled with no parent.
		void DrawShapeMenu(GameObject* parent);

		// The Cutscene submenu, each template made a child of parent and
		// selected. Disabled with no parent.
		void DrawCutsceneMenu(GameObject* parent);

		// The Rendering submenu, each object made a child of parent and
		// selected. Disabled with no parent.
		void DrawRenderingMenu(GameObject* parent);

		// What a new UiText is drawn in: the project's first font by path.
		std::string FirstFont() const;

		// What a new PreVertex runs: the project's first pre-vertex file by
		// path, relative to the project.
		std::string FirstPreVertex() const;

		// What a new shape or scene draws with: the new project's shader while
		// the project has it, or else its first, and none with no project open.
		std::string ShapeShader() const;
		static void SkipRows(size_t rows);

		// Finds the open nodes still there, under their parents in order, and
		// counts the rows each one's open subtree takes below its own.
		void GatherOpenNodes();

		// Whether node is drawn open. One that has gained a child since it was
		// last drawn opens.
		bool NodeOpen(const GameObject& node);
		void SetNodeOpen(const GameObject& node, bool open);

		// The eye at the right of the row just drawn, which disables or enables
		// the object. Faded when an ancestor already disables it.
		void DrawEnableToggle(GameObject* gameObject, bool parent_disabled, const ImRect& eye);

		// Takes a GameObject or an asset dropped on the tree node just drawn:
		// on its top or bottom edge to sit before or after it, anywhere else to
		// become its last child. A scene's row or a prefab's root takes drops
		// only into it. A GameObject moves to another scene only when both are
		// owned, since undo keeps the scenes it owns.
		void DragAndDropNode(GameObject* gameObject, bool open);

		// Takes the GameObject or asset being dragged to where a drop in zone
		// of row puts it. Whether it would, so the caller marks where.
		bool DropPlaced(GameObject& row, bool open, Hierarchy::Zone zone);

		void DrawRenameField(GameObject* gameObject);

		// For the hierarchy node just drawn: whether it was clicked, judged on
		// release so dragging a node into an inspector field leaves the inspector
		// on the object that holds the field. A press on the arrow only opens it.
		bool NodeReleased();

		#pragma endregion

		#pragma region Inspector

		// Each takes every selected GameObject, or the match on each, the one
		// drawn from first. A field they disagree on shows "-", and an edit
		// writes into them all.
		void DrawInspector();
		void DrawTransform(const std::vector<GameObject*>& objects, float column);
		void DrawThread(const std::vector<GameObject*>& objects, float column);
		void DrawComponent(const std::vector<GameObject*>& objects, const std::vector<ComponentBase*>& components);

		// Takes a component of gameObject dragged over onto, which fills area,
		// putting it above onto or below it by the half under the mouse, and
		// marks where with a line.
		void DropComponent(GameObject& gameObject, ComponentBase& onto, const ImRect& area);

		// Scrolls the Inspector on while a component is held near its top or
		// bottom, to reorder past what is in sight.
		void ScrollWhileReordering();
		void DrawComponentMenu(const std::vector<GameObject*>& objects, const ComponentMenu::Node& node);

		// The name prompt for a script of kind that goes on objects once it builds.
		void AskForScriptOn(AssetKind kind, const std::vector<GameObject*>& objects);

		// Right-click on a component in the Add Component menu: the category it
		// is filed under in this project.
		void DrawCategoryPopup(const std::string& component);

		struct SourceLine
		{
			// Empty when no header defines the type.
			std::string path;
			int line = 0;
		};

		// Where a component type is defined: the scripts project's header for a
		// script, otherwise the engine's own.
		SourceLine FindSource(const std::string& component) const;

		// Right-click on a component's header in the Inspector.
		void DrawSourcePopup(const std::string& component);

		// A DialogueManager's New Binding: a LOOM_DIALOGUE_BINDING function written into
		// the header defining type, opened there in Visual Studio.
		void AddDialogueBinding(const std::string& type);
		void DrawFields(const std::vector<LoomObject*>& objects, const std::vector<std::string>& labels, float column);

		// A reference field of object: what it holds, which a click swaps for
		// another picked from what it could hold in the scene or prefab object
		// is in, or in every scene when object is in none.
		// Takes such an object dragged onto it. Whether it changed.
		bool DrawReferenceField(const LoomObject& object, const SerializedField& field);

		void DrawModelPreview();

		#pragma endregion

		#pragma region Views

		void DrawPrefab();
		void DrawGame();
		void DrawDebug();

		// The views that can fill the monitor, a double-click on the tab of
		// their panel away.
		enum class View
		{
			Prefab,
			Game,
			Debug,
		};

		// What each view's panel holds, in whatever window is open: its
		// panel, or the one over the whole monitor.
		void DrawPrefabView();
		void DrawGameView();
		void DrawDebugView();

		// Whether the tab of the panel just begun, or its title bar while it
		// floats, was double-clicked this frame.
		static bool TabDoubleClicked();

		// Inside the panel of view: notes it as the one F11 fills the monitor
		// with when it has focus, and fills it on a double-click of its tab.
		void Watch(View view);

		// The bar along the top of the Game and Debug panels.
		void DrawToolbar();

		// Fills the rest of the panel with the scenes, seen through camera, or
		// through each scene's own when there is none. Only a view with a
		// camera takes the mouse and keyboard, to fly it, and only one with a
		// camera rings the selection, when it is given an outline to do it with.
		void DrawView(EditorViewport& view, const std::vector<Scene*>& scenes, EditorCamera* camera, SelectionOutline* outline = nullptr);

		void DrawSceneData();
		void DrawStats();

		#pragma endregion

		#pragma region Workspace

		void DrawGui();

		void DrawMainMenuBar();

		// What every window's menu bar holds, the editor's and each torn off.
		void DrawMenus();

		// Minimize, maximize and close, at the right of the menu bar, in place
		// of the title bar frame takes away; true when close is clicked.
		bool DrawWindowButtons(GLFWwindow* window, WindowFrame& frame);

		// The menu bar of a window torn off the editor, whose close closes its panels.
		void DrawTornBar(const TornWindows::Torn& torn);

		// The menu bar and the panels docked round the dock space.
		void DrawWorkspace();

		// One view over the whole monitor, in place of the workspace, until
		// F11, or Escape while nothing plays, brings the workspace back.
		// F11 in the workspace fills it with the view focused last.
		void DrawFullscreen();
		void EnterFullscreen(View view);
		void LeaveFullscreen();

		void HandleShortcuts();

		#pragma endregion

		#pragma region Screenshots

		// Writes the frame being drawn to the screenshot's path once it is
		// finished, and ends the run if the screenshot asked to.
		void CaptureFrame();

		#pragma endregion

		#pragma region Dialogs

		// The first-run "where are your scenes?" prompt, the new project dialog,
		// and the browsers behind every Browse... button.
		void DrawFolderPrompt();
		void DrawNewProjectPrompt();
		void DrawFileDialogs();

		// Runs then, after asking whether to save what is named unsaved
		// first, or at once when nothing is. Save leaves the question up when
		// save fails, and Cancel runs nothing. One question at a time.
		void AskToSave(
			std::vector<std::string> unsaved,
			std::function<bool()> save,
			std::function<void()> then);
		void DrawSaveQuestion();

		// Everything unsaved anywhere in the editor, by name, for SaveProject
		// to write.
		std::vector<std::string> UnsavedWork() const;
		std::vector<Scene*> UnsavedScenes() const;

		// Another project in place of this one, asking about the unsaved first.
		void LeaveFor(const std::string& project);

		#pragma endregion

		#pragma region History

		// Syncs the prefabs and records a step once a scene has changed since
		// the last time, which on a large scene takes long enough to be felt.
		void RecordHistory();
		bool CanStepHistory() const;
		bool CanUndo() const;
		bool CanRedo() const;

		// While playing, the Inspector's field at index of each of objects
		// changed from its before. The edits go in m_playHistory once they are
		// let go of, or another field is edited.
		void TrackPlayEdit(const std::vector<LoomObject*>& objects, size_t index, const std::vector<std::string>& before);
		void RecordPlayEdit();

		// Runs change, which adds, takes off or orders gameObject's
		// components, and while playing puts it in m_playHistory.
		void ChangeComponents(GameObject& gameObject, const std::function<void()>& change);

		// The same for each of objects, change given its index, as one step.
		void ChangeComponents(const std::vector<GameObject*>& objects, const std::function<void(size_t)>& change);

		// Undo or Redo, keeping the selection on its object when the step
		// takes it out and makes it again.
		void StepHistory(bool (SceneHistory::* step)(const std::vector<Scene*>&));

		// Whether input in the window can change a scene, so a step is
		// recorded for the history once it is let go. The Shaders and Sprites
		// panels and the Project panel's tabs but Scenes only edit, file and
		// open files: what is dragged out of them lands in another window.
		bool CanEditScenes(const ImGuiWindow* window) const;

		#pragma endregion

		#pragma region Snapshots

		// One open scene as the text it serializes to, so it can be put back
		// after the code behind it is replaced or after a run is thrown away.
		struct SceneSnapshot
		{
			std::string text;
			std::string path;
			bool active = false;
		};

		std::vector<SceneSnapshot> TakeSnapshots() const;
		void RestoreSnapshots(const std::vector<SceneSnapshot>& snapshots);

		// The scenes and prefab, and what was selected in them, as a rebuild
		// found them or as they were opened while it ran.
		void RestoreAfterBuild();

		// Moves the targets inside the open prefab over to m_attachAfterBuildInPrefab.
		void HoldPrefabAttachTargets();

		// The script Add Component made for an object, put on it.
		void AttachAfterBuild();

		// Switches updating on, keeping the state the scenes are in now so Stop
		// has something to go back to.
		void BeginPlay();

		#pragma endregion

		#pragma region Layouts

		void BuildDefaultLayout(unsigned int dockspace_id);

		// The panels the Window menu toggles, by the name ImGui knows each by.
		struct Panel
		{
			const char* title;
			bool* shown;
			bool shownByDefault;
		};

		std::vector<Panel> Panels();

		// Puts which panels are open into ImGui's settings, so the layout file
		// and every saved layout carry it along with the docking.
		void AddPanelSettings();

		// The Layout menu's Save prompt.
		void DrawSaveLayoutPrompt();

		// Remembered in the settings file, so the next session names it too.
		void SetCurrentLayout(const std::string& name);

		#pragma endregion

		#pragma region Queued Input

		// Records the item just drawn as where a queued import goes, if the
		// mouse is over it.
		void TakeImportTarget(GameObject* gameObject);

		// The same for the Scenes tab and the FileTabs, with the folder a
		// drop there goes in.
		void TakeImportFolder(const std::string& folder, bool hovered);

		// Into the folder the drop landed in, selecting the copy.
		void CopyIntoProject(const std::string& file);
		void ImportQueued();
		void ClickQueued();
		void RunQueuedTests();

		#pragma endregion

		#pragma region Assets

		// Rescans the project folder: the asset tree the Scenes tab draws,
		// the scenes in it, the shaders and each FileTab's files.
		void RefreshProjectAssets();

		// A model in the Model panel, anything else where
		// ProjectAssets::OpenerOf says; the shell's opener is whatever Windows
		// opens that kind of file with, and the text editor when nothing
		// claims it. A sprite opened as a preview takes the place of the one
		// before it.
		void OpenAsset(const std::string& path, bool preview = false);

		// In the scripts project's solution when it has one, at the line and
		// column when they are given, counting from one.
		void OpenInVisualStudio(const std::string& path, int line = 0, int column = 0);

		// ImGui's way of opening a link, which for a file in the project is
		// OpenAsset's.
		static bool OpenInShell(ImGuiContext* context, const char* path);

		// Lets a file be dragged out of the Scenes tab or a FileTab, onto a
		// field that names a file or a GameObject in the hierarchy, and a file
		// or folder onto another folder.
		void DragAsset(const AssetNode& node);

		// Inside a drag and drop target, takes a file or folder dropped there
		// into folder, or a GameObject out of the Hierarchy, saved there as a
		// prefab. Either waits for the end of the panel, since it rescans the
		// tree being drawn.
		void AcceptMove(const std::string& folder);
		void MoveAsset(const std::string& path, const std::string& folder);

		// The FileTabs file theirs by their paths, so one moved from from to to
		// stays filed where it was.
		void Refile(const std::string& from, const std::string& to);

		// The texture at path, to show in place of its icon. Null for anything
		// else, or a texture that will not load.
		const Texture* Thumbnail(const std::string& path);

		#pragma endregion

		#pragma region Asset Drops

		// Whether an asset dropped in the Hierarchy at path has a GameObject
		// to become under parent: a prefab not already around parent, a
		// model, a texture or sprite, or a sound.
		bool CanPlace(const std::string& path, const GameObject& parent) const;

		// The GameObject an asset CanPlace becomes, put and selected where
		// placement says. Null when it fails to load.
		GameObject* PlaceAsset(const std::string& path, const Hierarchy::Placement& placement);

		// AcceptAssetDrop for a shader or dialogue dragged out of the Scripts
		// tab, which lists them: path becomes the file's path from the project.
		bool AcceptScriptsDrop(std::string& path, ScriptEntryKind kind);

		// Gives the GameObject the shader, on its Material or a new one.
		void DropShader(GameObject* gameObject, const std::string& path);

		// Gives the GameObject the pixel shader, on its PixelShader or a new one.
		void DropPixelShader(GameObject* gameObject, const std::string& path);

		// Gives the GameObject the compute shader, on its ComputeShader or a new one.
		void DropComputeShader(GameObject* gameObject, const std::string& path);

		// Gives the GameObject the pre-vertex file, on its PreVertex or a new one.
		void DropPreVertex(GameObject* gameObject, const std::string& path);

		// What the Scripts tab drags: a script type's name, a shader's or
		// pre-vertex file's path from the project, or a folder's path.
		static constexpr const char* script_payload = "LoomScript";
		static constexpr const char* script_folder_payload = "LoomScriptFolder";

		// Takes a script or shader dragged out of the Scripts tab onto a
		// GameObject in the hierarchy, attaching it. Whether the drag was one.
		bool DropScript(GameObject* gameObject);

		#pragma endregion

		#pragma region Reloads

		// Recompiles the shaders whose files something else has written, every
		// so often.
		void PollShaders();

		// Has the Sprites that show a file the Sprites panel saved draw it as
		// it now is, and the 2D tab keep a palette renamed in the Palette
		// panel filed where it was.
		void ReloadSavedSprites();

		// Has the instances of a prefab the Model panel saved take it as it
		// now is.
		void TakeSavedModels();

		#pragma endregion

		#pragma region Asset Creation

		// Where the Create prompt's asset would be written: the file for most,
		// the source for a script, which goes under the scripts project even
		// when asked for somewhere else.
		std::string NewAssetPath() const;
		std::string NewAssetFolder() const;

		// The project's scripts project, or the one Create > Script would add
		// to a project that has none.
		std::string ScriptsProject() const;

		// Why the Create prompt's name cannot be used, or an empty string.
		std::string NewAssetProblem() const;

		// The path written, or an empty string when it could not be.
		std::string CreateAsset();

		#pragma endregion

		#pragma region Project

		// Reads <folder>/*.loomproject, if there is one, and hooks up the script
		// library it names.
		void LoadProjectFile();

		// Watches for a finished build and puts the scenes back once the new
		// library is in.
		void PollScripts();

		// Watches for the refresh's build to finish, and closes the editor
		// once it has, unless it failed.
		void PollRefresh();

		// Runs the update that waits, once it is due.
		void ApplyUpdate();

		// Takes back every question about saving, with what is unsaved kept.
		void DropSaveQuestions();

		// Every unsaved scene, shader and sprite, to its file. False when
		// something could not be saved.
		bool SaveProject();

		// SaveActiveScene's choice of file, for any owned scene.
		bool SaveToProject(Scene* scene);

		#pragma endregion

		#pragma region Settings

		// The chosen folder and the scene that was open in it, remembered beside
		// the executable so the prompt is a first-run thing rather than an
		// every-run thing, and so a session picks up where the last one left off.
		void LoadSettings();
		void SaveSettings() const;

		// Which of the project's scenes a session opens with: the one it was
		// last on, or its only one. None when there is a choice to make, which
		// the views then offer.
		std::string StartupScene() const;

		#pragma endregion

		#pragma region Scenes

		// Deletes every open scene here and now, rather than on the task queue:
		// the callers are about to unload the code those scenes are made of.
		void CloseAllScenes();

		// Starts reading the scene a slice a frame, in place of any other being
		// opened, so the views can show how far it has got.
		void BeginOpeningScene(const std::string& path);

		// Reads the next slice of the scene being opened. Once it is all read,
		// opens it and starts a run that was waiting on it.
		void StepOpening();

		// Reads the rest of the scene being opened here and now, and opens it.
		void FinishOpening();

		// Makes a scene just read from path one of the editor's own, and the
		// active one.
		void AdoptScene(Scene* scene, const std::string& path);

		// How far the project, the scene on its way or the scripts it waits for
		// have got, centred over the whole editor.
		void DrawLoading();

		// What a view holds with no scene in it: nothing while DrawLoading
		// shows, or else the project's scenes to pick from.
		void DrawNoScene();

		#pragma endregion

		#pragma region Selection

		// Selection and the active scene are raw pointers into a hierarchy that
		// edits itself between frames, so both are checked for liveness once per
		// frame rather than trusted.
		void ValidateSelection();

		// Whether the selection is root or under it. False once the selection is
		// gone, which can happen partway through a frame.
		bool IsSelectedIn(const GameObject& root) const;

		// Whether gameObject is any of those selected, not only the one the
		// Inspector is drawn from.
		bool IsSelected(const GameObject& gameObject) const;

		// The guids of what is selected, in GetSelection's order, and the
		// selection again from them once those still there are found, or as
		// it is when none is.
		std::vector<Guid> GetSelectedGuids() const;
		void SelectAgain(const std::vector<Guid>& guids);

		// Every GameObject row the Hierarchy has under its open nodes, in the
		// order it draws them.
		std::vector<GameObject*> HierarchyRows() const;

		// Makes the selection what a click or key in the Hierarchy asked for.
		void ApplySelection(const ImGuiMultiSelectIO& io);

		// Whether the Hierarchy draws node open, without counting it as drawn.
		bool ShownOpen(const GameObject& node) const;

		#pragma endregion

		#pragma region Prefabs

		// The prefab as the file now has it, read again whenever the file
		// changes. A change from outside the editor unlinks the prefab's
		// instances, so they take the file, and has the Prefab panel show it.
		// Null when there is no such prefab.
		const Prefabs::ObjectNode* LoadPrefab(const std::string& path);
		bool WritePrefab(const std::string& path, const Prefabs::ObjectNode& prefab);
		std::filesystem::path PrefabFile(const std::string& path) const;

		// The file of the prefab gameObject is part of, whether as an instance
		// or in the Prefab panel. Empty when it is in none.
		std::filesystem::path PrefabFileOf(const GameObject& gameObject) const;

		// Run with the history: carries an edit saved in the Prefab panel or
		// made to an instance to the file and every instance, and brings instances
		// that do not know their prefab yet into line with it. Only the
		// prefabs of instances changed, of files written and of the panel
		// edited are looked at.
		void SyncPrefabs(const std::unordered_set<GameObject*>& changed);

		// Whether SyncPrefabs has work with no scene changed: a prefab file
		// written from outside, a Prefab panel edited, saved or still to agree
		// with its file.
		bool PrefabsNeedSync() const;

		// The prefabs read so far whose files were written since.
		std::vector<std::string> ChangedPrefabFiles() const;

		// The Prefab panel's copy made the file's again, in place: the same
		// guids, so the selection and the panel's focus stay where they are.
		void ReshowPrefab();

		// The Prefab panel's copy made subtree, a text of its prefab, in
		// place. False, with the panel closed, when it does not show.
		bool ShowPrefab(const std::string& subtree);

		// The Prefab panel's unsaved edits, kept as text while a build has its
		// scene down, and OpenPrefab after it with them.
		void HoldPrefabEdits();
		void ReopenPrefab(const std::string& path, const std::string& held, const std::string& shape);

		// Undo or Redo in the Prefab panel's copy.
		void StepPrefab(bool undo);

		// Whether Ctrl+Z, Ctrl+Y and Ctrl+S are the Prefab panel's: it has
		// the keyboard, or what is selected is in it.
		bool PrefabHasKeyboard() const;

		// Asks whether to save the Prefab panel's edits before it closes or
		// shows another prefab.
		void DrawPrefabQuestion();

		// Whether gameObject is in the prefab at path: its scene's copy in the
		// Prefab panel, or an instance of it. A prefab dropped there would
		// take in its own every edit.
		bool IsInPrefab(const GameObject* gameObject, const std::string& path) const;

		// The prefab tree drawn in the Hierarchy, under the open scenes.
		void DrawPrefabNode();

		#pragma endregion

		#pragma region Views

		EditorViewport m_gameView;
		EditorViewport m_debugView;

		EditorCamera m_debugCamera;

		SelectionOutline m_debugOutline;

		// Shared: unlike an outline, it keeps nothing the size of a view.
		ColliderGizmos m_colliderGizmos;

		#pragma endregion

		#pragma region Prefabs

		// The Prefab panel: the prefab open in it, by its path from the project
		// folder, the scene it is shown in, whose first child is the prefab
		// and second the light, and the prefab as the panel last agreed with
		// its file, which its held edits differ from until saved.
		EditorViewport m_prefabView;
		EditorCamera m_prefabCamera;
		SelectionOutline m_prefabOutline;
		Scene* m_prefabScene = nullptr;
		Guid m_prefabRoot{ };
		std::string m_prefabPath{ };
		std::string m_prefabShape{ };
		int m_prefabFocusFrames = 0;

		// Set when the file under the Prefab panel changed outside the editor.
		bool m_reshowPrefab = false;

		// Undo over the Prefab panel's copy, as its text. Its edits are held
		// there until saved, the file and instances staying as they were.
		EditHistory<std::string> m_prefabHistory{ };
		bool m_prefabOpened = false;
		bool m_savePrefab = false;
		bool m_prefabUnsaved = false;
		bool m_prefabFocused = false;

		// What the Prefab panel shows once the question about its unsaved
		// edits is answered: another prefab's path, or empty to close.
		std::optional<std::string> m_prefabNext{ };

		// AskToSave's question, until it is answered.
		struct SaveQuestion
		{
			std::vector<std::string> unsaved{ };
			std::function<bool()> save{ };
			std::function<void()> then{ };
		};

		std::optional<SaveQuestion> m_saveQuestion{ };

		// The open prefab, while a build has its scene down.
		std::string m_prefabBeforeBuild{ };
		std::string m_prefabHeldBeforeBuild{ };
		std::string m_prefabShapeBeforeBuild{ };

		// Each prefab file read so far, by its path from the project folder,
		// with when it was written as it was read.
		struct LoadedPrefab
		{
			Prefabs::ObjectNode tree;
			std::filesystem::file_time_type written;
		};

		std::map<std::string, LoadedPrefab> m_prefabs{ };

		// Each instance as the last pass read it, by its GameObject's guid:
		// parsing a model's vertices again on every edit is most of a pass.
		// Read again once it changes.
		std::unordered_map<Guid, Prefabs::ObjectNode> m_readInstances{ };

		// The outermost instances of each prefab seen, by its path, as their
		// scene and guid, checked against the scenes as they are when used.
		std::map<std::string, std::set<std::pair<Scene*, Guid>>> m_instances{ };

		// The Prefab panel's copy as the last pass left it.
		std::string m_panelText{ };

		// The instances known to agree with their prefab, by the guid of the
		// Prefab component linking them and the path it names. One not in here
		// takes its prefab's content; one in here that differs from it has been
		// edited, and its edit goes to the prefab and every other instance.
		std::set<std::pair<std::string, std::string>> m_linkedInstances{ };

		#pragma endregion

		#pragma region Builds

		ScriptLibrary m_scripts;
		EditorRefresh m_refresh;
		EditorUpdate m_update{ };
		bool m_updateSaveFailed = false;
		bool m_relaunch = false;
		WebPlayer m_webPlayer;
		GameBuilder m_gameBuilder;
		BuildSettings m_buildSettings;

		std::unique_ptr<EditorFileDialogs> m_dialogs;

		#pragma endregion

		#pragma region Scenes

		Scene* m_activeScene = nullptr;
		GameObject* m_selected = nullptr;
		uint64_t m_selectedId = 0;

		// The ids of every GameObject selected, m_selected's among them, and
		// m_selectedId when the Hierarchy last saw it, so a selection made
		// elsewhere is where its Shift+click range starts.
		std::vector<uint64_t> m_selection{ };
		uint64_t m_rangeSourceFor = 0;
		bool m_nodePressOpened = false;
		std::vector<std::string> m_pendingSelection{ };
		bool m_pendingSelectionAdds = false;
		int m_selectionFramesLeft = 0;
		std::vector<Scene*> m_ownedScenes{ };

		// Where each open scene came from, so a scene can be put back after the
		// script library it depends on is rebuilt.
		std::map<Scene*, std::string> m_scenePaths{ };

		// The scene being opened a slice a frame, and the file it is read from.
		std::unique_ptr<SceneSerializer::Loading> m_opening{ };
		std::string m_openingPath{ };

		// A project on its way in, and what it waits on.
		std::optional<ProjectLoad> m_projectLoad{ };

		int m_sceneCounter = 0;

		#pragma endregion

		#pragma region Project

		std::string m_projectPath{ };
		std::string m_projectFile{ };

		// When PollShaders last looked, in ImGui's seconds.
		double m_shadersPolled = 0.0;
		std::string m_projectName{ };
		std::vector<std::string> m_projectScenes{ };

		// Every file in the project from the last scan, by its path from the
		// project folder.
		std::vector<std::string> m_projectFiles{ };

		// Every folder in the project from the last scan, the same way.
		std::vector<std::string> m_projectFolders{ };

		// The shaders from the last scan, which the Scripts tab lists.
		std::vector<std::string> m_projectShaders{ };
		std::vector<std::string> m_projectDialogues{ };
		std::vector<std::string> m_projectPreVertices{ };

		#pragma endregion

		#pragma region Project Panel

		// A model file picked in the 3D tab, previewed in the Inspector
		// in place of a GameObject.
		std::string m_selectedModel{ };
		ModelPreview m_modelPreview;

		// The project's scenes and prefabs as they stood at the last scan.
		AssetNode m_assets{ };
		FolderWatch m_projectWatch;
		KindFilter m_projectFilter;
		std::string m_selectedAsset{ };
		SearchBar m_assetSearch{ };

		// Textures that would not load, so each is tried once per scan.
		std::unordered_set<std::string> m_noThumbnail{ };
		bool m_revealSelectedAsset = false;

		// The file PingAsset last flashed, and when.
		std::string m_pingedAsset{ };
		double m_pingedSince = 0.0;

		// A file or folder dropped on a folder, and that folder.
		std::string m_assetToMove{ };
		std::string m_assetMoveFolder{ };

		// A GameObject dropped on a folder, to be saved in it as a prefab.
		Guid m_objectToSave{ };

		// A file double-clicked or picked Open on. Opened once the tree is
		// drawn, since opening one can rescan the tree being drawn.
		std::string m_assetToOpen{ };

		#pragma region Scripts Tab

		// Where a script or shader is written, what the Scripts tab calls it,
		// and whether it has been compiled.
		struct ScriptEntry
		{
			std::string file;
			std::string label;
			ScriptEntryKind kind = ScriptEntryKind::Script;
			bool compiled = false;
		};

		// Each type the scripts project's headers declare as a script or its
		// library registered, by name, and each of the project's shaders, by its
		// path from the project.
		std::map<std::string, ScriptEntry> m_scriptEntries{ };
		ScriptFolders m_scriptFolders;
		KindFilter m_scriptFilter;

		// Saves to the headers rescan the Scripts tab.
		FolderWatch m_scriptWatch;
		std::string m_selectedScript{ };
		bool m_revealSelectedScript = false;
		FolderTree m_scriptFolderTree;

		// The Asset and reference field pickers' search and open folders,
		// apart from any tab's own.
		FolderTree m_fieldPicker;

		// Set while the Inspector draws a field the selection disagrees on, so
		// a picker shows "-" for what it holds.
		bool m_mixedField = false;

		// Where the Create prompt's script is filed once it is written.
		std::string m_newScriptFolder{ };

		#pragma endregion

		std::vector<FileTab> m_fileTabs{ std::begin(FileTabs::all), std::end(FileTabs::all) };

		#pragma endregion

		#pragma region Settings

		// The scene a session opens with: what the settings file said last time,
		// and then whichever one this session last made active.
		std::string m_startupScene{ };

		// The graphics API, font and look, edited in the Settings window.
		EditorSettings m_settings;

		#pragma endregion

		#pragma region Play

		// One entry per scene that was open when a rebuild started, and what a
		// Stop pressed during that rebuild leaves behind for it to come back to.
		std::vector<SceneSnapshot> m_snapshots{ };
		std::vector<Guid> m_selectedBeforeBuild{ };
		bool m_playAfterCompile = false;

		// The scenes as they were when Play was pressed, held for as long as the
		// play session lasts: a paused session is still one Stop away from the
		// state it started in. Play sets the flag rather than the editor reading
		// it off the snapshots, which are empty for a session started with no
		// scene open.
		bool m_playing = false;
		std::vector<SceneSnapshot> m_playSnapshots{ };

		// Set by "Step": updating is switched on for exactly one frame and
		// switched back off at the top of the next one.
		bool m_steppedLastFrame = false;

		#pragma endregion

		#pragma region History

		SceneHistory m_history{ };
		PlayHistory m_playHistory{ };

		// The field being edited in the Inspector while the game runs, with
		// the text it held before, until the edit is let go of.
		struct PlayEdit
		{
			LoomObject* object = nullptr;
			uint64_t id = 0;
			size_t index = 0;
			std::string before{ };
		};

		std::vector<PlayEdit> m_playEdits{ };

		// Set when a click lands or a widget is let go of in a window that
		// CanEditScenes, or a scene arrives; a step is recorded on the first
		// frame after that with nothing held down.
		bool m_historyDirty = true;

		// Whether a view's camera is being flown this frame or was just let go
		// of. That edits nothing, so it marks nothing, since comparing a large
		// scene takes long enough to stall the editor.
		bool m_flyingView = false;

		#pragma endregion

		#pragma region Panels

		bool m_showProject = true;
		bool m_focusProject = false;
		bool m_projectFocused = false;

		// A 2D file's preview brought the Sprites panel forward: the keyboard
		// comes back to the list once it has.
		bool m_previewingFile = false;

		// What the Project panel deleted, while Ctrl+Z can still put it back.
		AssetTrash m_assetTrash;

		// The Project panel's tab in front, and the one to bring forward on
		// the next frame, or none: each one of ProjectTabs.
		std::string_view m_projectTab = ProjectTabs::Scenes;
		std::string_view m_projectTabToShow{ };

		// How narrow the Project panel goes and still shows every tab's icon,
		// as it was last drawn, or none before it has its tabs.
		float m_projectMinWidth = 0.0f;

		bool m_showHierarchy = true;
		bool m_hierarchyFocused = false;
		bool m_showInspector = true;
		bool m_showGame = true;
		bool m_showDebug = true;
		bool m_gamePanelFocused = false;
		bool m_gamePanelHovered = false;
		GameFocus m_gameFocus;
		TabHistory m_tabHistory;
		std::optional<View> m_fullscreen;
		View m_lastView = View::Game;
		WindowFullscreen m_windowFullscreen;
		WindowFrame m_windowFrame;
		TornWindows m_tornWindows;

		// By viewport, and gone before the window it is on.
		std::unordered_map<unsigned int, std::unique_ptr<WindowFrame>> m_tornFrames;
		double m_fullscreenSince = 0.0;
		bool m_showSceneData = true;
		bool m_showConsole = true;
		bool m_showShaders = false;
		bool m_showDialogues = false;
		bool m_showSprites = false;
		bool m_showModels = false;
		bool m_showPalette = false;
		bool m_showPrefab = false;
		bool m_showStats = true;
		bool m_showKeyBindings = true;
		bool m_showImGuiDemo = false;
		bool m_showSettings = false;
		bool m_showBuildSettings = true;
		bool m_showTests = true;
		bool m_focusBuildSettings = false;

#ifndef LOOM_FINAL
		// Whether this frame drew the stages, so the profiler records them,
		// and only then.
		bool m_stagesShown = false;
#endif

		#pragma endregion

		#pragma region Layouts

		bool m_firstFrame = true;
		bool m_rebuildLayout = false;

		// Set when a layout has been put in place, so the next frame checks it
		// has every panel; the first frame's is the one the last run left.
		bool m_checkLayout = false;

		// The windows a loaded layout has a place for. Empty for the layout the
		// last run left, which ImGui's own settings answer for.
		std::vector<unsigned int> m_layoutWindows{ };

		// Saved once the panels have been drawn, for --save-layout.
		std::string m_layoutToSave{ };

		// Loaded at the top of the next frame, before the dockspace it replaces
		// is submitted.
		std::string m_pendingLayout{ };

		// What the Layout menu lists, read again each time it opens.
		std::vector<std::string> m_layouts{ };

		// The saved layout last loaded or saved, shown on the Layout menu and
		// offered as the Save prompt's name. Empty once the layout is reset.
		std::string m_currentLayout{ };

		bool m_askForLayoutName = false;
		char m_layoutName[EditorLayouts::MaxNameLength + 1]{ };

		#pragma endregion

		#pragma region Inspector

		static constexpr size_t max_category_length = 128;

		ComponentMenu m_componentMenu;
		char m_categoryBuffer[max_category_length + 1]{ };
		bool m_openAddComponent = false;

		// The part of a pixel ScrollWhileReordering has yet to scroll.
		float m_reorderScroll = 0.0f;

		// The source of the component whose popup is open, found as it opens.
		SourceLine m_componentSource{ };

		char m_nameBuffer[128]{ };
		const GameObject* m_nameBufferOwner = nullptr;

		#pragma endregion

		#pragma region Dialogs

		bool m_askForFolder = false;

		// What File > Open Demo lists, read again each time it opens.
		std::vector<std::filesystem::path> m_demos{ };
		char m_folderBuffer[512]{ };

		bool m_askForNewProject = false;
		char m_newProjectFolder[512]{ };
		char m_newProjectName[128]{ "MyGame" };

		bool m_askForAsset = false;
		bool m_focusAssetName = false;
		AssetKind m_newAssetKind = AssetKind::Folder;
		std::string m_newAssetFolder{ };
		char m_newAssetName[128]{ };
		std::string m_newAssetProblem{ };

		// An asset field's New, and what it made, which the field takes up when next
		// drawn. A new selection drops both.
		void* m_newAssetFor = nullptr;
		std::string m_newAssetMade{ };

		// Add Component's New makes a script for the object, as do StateMachine and
		// DialogueManager rather than attaching one, and the script attaches to it
		// once the compile that follows has loaded it.
		std::vector<Guid> m_newScriptFor{ };
		std::vector<Guid> m_attachAfterBuildTo{ };

		// A reopened prefab has new guids, so its targets are child indices from its root.
		std::vector<std::vector<size_t>> m_attachAfterBuildInPrefab{ };
		std::string m_attachAfterBuildPrefab{ };

		std::string m_attachAfterBuild{ };
		bool m_compileAfterBuild = false;

		#pragma endregion

		#pragma region Tools

		EditorConsole m_console;
		KeyBindings m_keyBindings;
		ShaderEditor m_shaders;
		DialogueEditor m_dialogues;
		SpriteEditor m_sprites;
		ModelEditor m_models;
		TestsPanel m_tests;

		ExtensionLibrary m_extensions;

		#pragma endregion

		#pragma region Queued Input

		// A file waiting to be imported, and where the mouse goes first when it
		// came from a drop rather than a plain queue.
		struct QueuedImport
		{
			std::string path;
			bool positioned = false;
			float x = 0.0f;
			float y = 0.0f;
		};

		std::vector<QueuedImport> m_importQueue{ };
		GameObject* m_importTarget = nullptr;

		// The Project panel folder a drop landed in, which takes copies of the
		// files rather than importing them into a scene.
		std::string m_importFolder{ };

		// Whether the mouse has been put where the front drop happened, so the
		// panels drawn since have hovered what it lands in.
		bool m_importPlaced = false;

		// True for an undo, false for a redo.
		std::vector<bool> m_historySteps{ };

		struct QueuedClick
		{
			float x = 0.0f;
			float y = 0.0f;
			float toX = 0.0f;
			float toY = 0.0f;

			// Which of its events goes next, one a frame.
			int step = 0;
		};

		std::vector<QueuedClick> m_clicks{ };
		bool m_focusTests = false;

		// Whether the queued tests already compiled the scripts, so a failed
		// build runs them on the library it left rather than building again.
		bool m_testsCompiled = false;

		#pragma endregion

		#pragma region Hierarchy

		// The Hierarchy's open nodes, by m_ID so each copy of a scene opened
		// twice has its own, and with their GUIDs: an undone delete and a
		// prefab read again make new objects under the GUIDs they had. Scene
		// roots start open, so those closed are kept instead.
		std::unordered_map<uint64_t, Guid> m_openNodes{ };
		std::unordered_set<uint64_t> m_closedRoots{ };

		// This frame's open nodes under each node, with their sibling indices,
		// and the rows below each one.
		Hierarchy::OpenUnder m_openUnder{ };
		Hierarchy::RowsBelow m_rowsBelow{ };

		// How many children each row had when drawn last frame, and this frame.
		std::unordered_map<uint64_t, size_t> m_childCounts{ };
		std::unordered_map<uint64_t, size_t> m_drawnCounts{ };
		// Double-clicking the selection in the hierarchy edits its name in place,
		// in the same buffer as the inspector's name box.
		bool m_renaming = false;
		bool m_focusRename = false;
		unsigned int m_renameFieldId = 0;

		#pragma endregion

		#pragma region Screenshots

		std::string m_screenshotPath{ };
		std::string m_screenshotWindow{ };
		int m_screenshotFrame = 0;
		bool m_screenshotExits = false;
		int m_frame = 0;

		#pragma endregion
	};
};
