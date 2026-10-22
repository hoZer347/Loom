#pragma once

#include "ColliderGizmos.h"
#include "ComponentMenu.h"
#include "EditorCamera.h"
#include "EditorConsole.h"
#include "KeyBindings.h"
#include "EditorLayouts.h"
#include "EditorRefresh.h"
#include "EditorViewport.h"
#include "FolderWatch.h"
#include "KindFilter.h"
#include "GameFocus.h"
#include "Guid.h"
#include "Hierarchy.h"
#include "EditorSettings.h"
#include "ShaderEditor.h"
#include "SpriteEditor.h"
#include "ModelEditor.h"
#include "ModelPreview.h"
#include "Prefabs.h"
#include "SelectionOutline.h"
#include "SceneHistory.h"
#include "SearchBar.h"
#include "FileTab.h"
#include "FolderTree.h"
#include "ScriptFolders.h"
#include "ScriptLibrary.h"
#include "WebPlayer.h"
#include "WindowFullscreen.h"

#include <filesystem>
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

	enum class AssetKind
	{
		Folder,
		Scene,
		Prefab,
		Script,
		State,
		StateMachine,
		Shader,
		Texture,
		Palette,
	};

	// What the Scripts tab lists.
	enum class ScriptEntryKind
	{
		Script,
		State,
		Shader,
	};

	/**
	* Loom::Editor
	* - A Unity-style shell around the Loom runtime
	* - Owns one dock space holding the scene hierarchy, an inspector, a console
	*   and, in the centre, three views rendered into off-screen targets so they
	*   sit inside the layout like any other panel: the active scene on its own
	*   in a window named after it, where editing it edits every copy of it
	*   that is open, the way a prefab works; the game as its own cameras see
	*   it; and everything loaded, from a camera the editor flies
	* - Works out of a project folder, which the Project panel lists in tabs:
	*   its scenes and prefabs in the Scenes tab, which also creates them, its
	*   scripts and shaders in the Scripts tab, which creates those, and its
	*   textures, models, sounds and fonts each in a FileTab, the 2D tab
	*   creating textures. The
	*   scripts are a C++ project that builds into a library the editor loads
	* - Reads and writes scenes through SceneSerializer, and shows the text form
	*   of the open scene live in its Scene Data panel
	* - Takes over Engine's GUI hook on construction and hands it back on
	*   destruction, so nothing else has to know the editor exists
	*/
	struct Editor final
	{
		Editor();
		~Editor();

		Editor(const Editor&) = delete;
		Editor& operator=(const Editor&) = delete;

		// Scenes created or loaded here are owned by the editor and can be
		// closed from the Hierarchy; scenes created anywhere else still show up
		// in it, they just cannot be closed from it.
		Scene* CreateScene(const std::string& name = "New Scene");
		Scene* OpenScene(const std::string& path);
		bool SaveScene(Scene* scene, const std::string& path);
		void CloseScene(Scene* scene);

		// What the Hierarchy drags: a GameObject's guid rather than its address,
		// so a drop resolves against what still exists when the mouse comes up.
		static constexpr const char* object_payload = "LoomObject";

		// What double-clicking a scene in the Project panel does: every open
		// scene is saved and closed, so this one is all there is. Stops a run
		// first. Nothing closes if something will not save.
		Scene* OpenSceneAlone(const std::string& path);

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
		// only the panel draws, with a light the Hierarchy leaves out.
		void OpenPrefab(const std::string& path);
		void ClosePrefab();
		GameObject* GetPrefabRoot() const;

		// File > Save Scene and Ctrl+S: the active scene, to the file it was
		// opened from, or, for one never saved, a new file named after it in
		// the project's scenes folder. Refused while playing, when the scene
		// holds what the run has done to it.
		void SaveActiveScene();

		// File > Full Refresh and Ctrl+Shift+R: rebuilds the editor from the
		// engine's sources, then stops any run, saves the project and closes,
		// for main to start the new build on it. A failed build leaves this
		// editor running.
		void FullRefresh();
		bool IsRefreshing() const { return m_refresh.IsRunning() || m_relaunch; };

		// Whether the editor closed to be started again, from which executable
		// and on what: the scene that was active, or the project when none was.
		bool WantsRelaunch() const { return m_relaunch; };
		const std::filesystem::path& RelaunchExecutable() const { return m_refresh.GetExecutable(); };
		std::string RelaunchTarget() const;

		// The folder the editor works out of. Everything under it is listed in
		// the Project panel; the choice is remembered between runs, so
		// the folder prompt only shows up when there is nothing to remember.
		// A .loomproject file opens the folder it is in, after giving it a
		// scripts project if it has none, and a .loomscene file opens its
		// project on that scene.
		bool OpenProject(const std::string& path, bool open_scene = true);

		// Opens one of the projects in <engine>/Demos, by its folder name.
		bool OpenDemo(const std::string& name);

		// In a tab of the Shaders panel, which comes forward.
		void OpenShader(const std::string& path);

		// A layout from the Layout menu: loaded at the top of the next frame,
		// saved once the panels have been drawn, or asked a name for.
		void LoadLayout(const std::string& name) { m_pendingLayout = name; };
		void SaveLayout(const std::string& name) { m_layoutToSave = name; };
		void ShowSaveLayoutPrompt();
		// In a tab of the Sprites panel, which comes forward.
		void OpenSprite(const std::string& path);

		// A prefab, by its path in the project or on disk, in a tab of the
		// Model panel, which comes forward.
		void OpenModel(const std::string& path);

		// In the Palette panel, which comes forward, for the sprite in front.
		void OpenPalette(const std::string& path);

		// The Inspector's Add Component menu, opened on the selection the next
		// time it is drawn.
		void ShowAddComponent() { m_openAddComponent = true; };

		// Files a component under category in the open project, or back under
		// its own when category is empty.
		void SetComponentCategory(const std::string& component, const std::string& category);

		// Writes a new project (scenes, scripts, a Visual Studio solution) into
		// folder/name and opens it.
		bool CreateProject(const std::string& folder, const std::string& name);

		// Builds the project's script library and loads the result. The open
		// scenes go out and come back around it, because their components are
		// code that is about to be replaced.
		void CompileScripts();

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

		// Writes the editor as it stands - panels and all - to a bitmap once the
		// given number of frames have gone by, so a scene can be looked at
		// without a visible window. Given a window, only the first shown one
		// whose name contains it.
		void RequestScreenshot(const std::string& path, int after_frames, bool exit_after, const std::string& window = "");

		// Imports a model file once the next frame has drawn the panels, into
		// whatever the mouse is over: a Hierarchy node, one of the views, or
		// failing those the selection.
		void QueueImport(const std::string& path);

		// The same, with the mouse moved to x, y first, in ImGui's coordinates.
		// Over the Scenes tab or a FileTab, the file is copied into the
		// folder there instead.
		void Drop(const std::string& path, float x, float y);

		// File > Open Project..., starting beside the project that is open.
		void ShowOpenProjectDialog();

		// Window > Settings.
		void ShowSettings() { m_showSettings = true; };

		void SetActiveScene(Scene* scene);

		// Makes the scene active and brings the Scene view, opened if closed,
		// to the front.
		void ShowInSceneView(Scene* scene);

		// Deselects any GameObject; the Inspector shows the model instead.
		void SelectModel(const std::string& path);

		// Clears the selected model too.
		void Select(GameObject* gameObject);

		// Steps the owned scenes back or forward through the edits made to them.
		// Not while playing: what a run changes is not an edit.
		void Undo();
		void Redo();

		// An undo or redo to run once the queued imports have landed and been
		// recorded, one per frame, so an automated run can step back over them.
		void QueueHistoryStep(bool undo);

		// A left click at x, y in ImGui's coordinates, let go at to_x, to_y,
		// once the scripts are built, so an automated run can click through
		// the panels without the real mouse.
		void QueueClick(float x, float y, float to_x, float to_y);

		// The graphics API the settings file says to start on, which main reads
		// before the Engine opens its window. OpenGL when nothing is saved.
		static Backend SavedBackend();

		// The selection as the text a scene writes it as, on the system
		// clipboard, so it can be pasted into another scene or another editor.
		// A paste lands beside the selection.
		void Cut();
		void Copy();
		void Paste();

		void DeleteSelected();
		// Selects the GameObject with this name once its scene has loaded, so a
		// screenshot can show it in the inspector.
		void RequestSelection(const std::string& name);

		// Create > Script, State or State Machine in the project folder, named
		// and confirmed.
		void CreateScriptAsset(AssetKind kind, const std::string& name);

		// Opens the Create prompt for that kind of asset, named the first default
		// name nothing is using.
		void AskForAsset(AssetKind kind, const std::string& folder);

		const std::string& GetProjectPath() const { return m_projectPath; };

		// What EditorMcp reads the editor through.
		Scene* GetActiveScene() const { return m_activeScene; };
		GameObject* GetSelected() const { return m_selected; };
		const ScriptLibrary& GetScripts() const { return m_scripts; };
		bool IsOwned(Scene* scene) const;

		// The file an owned scene was opened from or saved to, empty for one
		// never saved.
		std::string ScenePathOf(Scene* scene) const;

		// An edit made other than through the panels, recorded for undo like
		// theirs once nothing is held down.
		void MarkEdited() { m_historyDirty = true; };

	private:
		void DrawGui();

		void DrawMainMenuBar();

		// The Project panel, its tabs drawn by the ones below.
		void DrawProject();

		// Brings the Project panel forward with one of ProjectTabs in front.
		void ShowProjectTab(std::string_view tab);

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
		void SaveInputActions();

		// Rereads the scripts project's headers for the types they declare.
		void RefreshScripts();
		void DrawHierarchy();
		void DrawSceneNode(Scene* scene);
		// A prefab instance's row shows the prefab icon, and it and everything
		// under it take the accent colour.
		void DrawGameObjectNode(GameObject* gameObject, bool parent_disabled, bool parent_in_prefab);

		// Only the rows in view are drawn, and the rest are stepped over, so the
		// tree costs what is open and on screen rather than what the scenes hold.
		void DrawChildNodes(GameObject& parent, bool disabled, bool in_prefab);
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

		// Takes a GameObject dropped on the tree node just drawn: on its top or
		// bottom edge to sit before or after it, anywhere else to become its last
		// child. Only within one scene, since undo keeps one scene per step.
		void DragAndDropNode(GameObject* gameObject, bool open);

		void DrawRenameField(GameObject* gameObject);

		// For the hierarchy node just drawn: whether it was clicked, judged on
		// release so dragging a node into an inspector field leaves the inspector
		// on the object that holds the field. A press on the arrow only opens it.
		bool NodeReleased();

		void DrawInspector();
		void DrawTransform(GameObject* gameObject, float column);
		void DrawThread(GameObject* gameObject, float column);
		void DrawComponent(GameObject* gameObject, ComponentBase* component);
		void DrawComponentMenu(GameObject* gameObject, const ComponentMenu::Node& node);

		// Right-click on a component in the Add Component menu: the category it
		// is filed under in this project.
		void DrawCategoryPopup(const std::string& component);
		void DrawFields(LoomObject& object, const std::vector<std::string>& labels, float column);
		void DrawModelPreview();
		void DrawScene();
		void DrawPrefab();
		void DrawGame();
		void DrawDebug();

		// The views that can fill the monitor, a double-click on the tab of
		// their panel away.
		enum class View
		{
			Scene,
			Prefab,
			Game,
			Debug,
		};

		// What each view's panel holds, in whatever window is open: its
		// panel, or the one over the whole monitor.
		void DrawSceneView();
		void DrawPrefabView();
		void DrawGameView();
		void DrawDebugView();

		// Whether the tab of the panel just begun, or its title bar while it
		// floats, was double-clicked this frame.
		static bool TabDoubleClicked();

		// Inside the panel of view: notes it as the one F11 fills the monitor
		// with when it has focus, and fills it on a double-click of its tab.
		void Watch(View view);

		// The menu bar and the panels docked round the dock space.
		void DrawWorkspace();

		// One view over the whole monitor, in place of the workspace, until
		// F11, or Escape while nothing plays, brings the workspace back.
		// F11 in the workspace fills it with the view focused last.
		void DrawFullscreen();
		void EnterFullscreen(View view);
		void LeaveFullscreen();

		// The bar along the top of the Game and Debug panels.
		void DrawToolbar();

		// Fills the rest of the panel with the scenes, seen through camera, or
		// through each scene's own when there is none. Only a view with a
		// camera takes the mouse and keyboard, to fly it, and only one with a
		// camera rings the selection, when it is given an outline to do it with.
		void DrawView(EditorViewport& view, const std::vector<Scene*>& scenes, EditorCamera* camera, SelectionOutline* outline = nullptr);
		void DrawSceneData();
		void DrawStats();

		// What each thread spent on each stage of the frame, in a debug build:
		// an average frame of the window laid out in time, then for each thread
		// its stages stacked over the trend and each one's average and peak.
		void DrawStages();

		// The first-run "where are your scenes?" prompt, the new project dialog,
		// and the browsers behind every Browse... button.
		void DrawFolderPrompt();
		void DrawNewProjectPrompt();
		void DrawFileDialogs();

		void HandleShortcuts();

		// Syncs the prefabs and records a step once a scene has changed since
		// the last time, which on a large scene takes long enough to be felt.
		void RecordHistory();
		bool CanStepHistory() const;

		// Undo or Redo, keeping the selection on its object when the step
		// takes it out and makes it again.
		void StepHistory(bool (SceneHistory::* step)(const std::vector<Scene*>&));

		// Whether input in the window can change a scene, so a step is
		// recorded for the history once it is let go. The Shaders and Sprites
		// panels and the Project panel's tabs but Scenes only edit, file and
		// open files:
		// what is dragged out of them lands in another window.
		bool CanEditScenes(const ImGuiWindow* window) const;

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

		// The scenes, and what was selected in them, as a rebuild found them.
		void RestoreAfterBuild();

		void BuildDefaultLayout(unsigned int dockspace_id);

		// The panels the Window menu toggles, by the name ImGui knows each by.
		struct Panel
		{
			// One of EditorPanels'.
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

		// Rescans the project folder: the asset tree the Scenes tab draws,
		// the scenes in it, the shaders and each FileTab's files.
		void RefreshProjectAssets();

		// Where ProjectAssets::OpenerOf says; the shell's opener is whatever
		// Windows opens that kind of file with, and the text editor when
		// nothing claims it.
		void OpenAsset(const std::string& path);

		// In the scripts project's solution when it has one.
		void OpenInVisualStudio(const std::string& path);

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

		// The texture at path, to show in place of its icon. Null for anything
		// else, or a texture that will not load.
		const Texture* Thumbnail(const std::string& path);

		// Takes a prefab dragged out of the Project panel onto a GameObject in
		// the hierarchy. Whether the drag was a file, taken or turned away.
		bool DropAsset(GameObject* gameObject);

		// AcceptAssetDrop for a shader dragged out of the Scripts tab, which
		// lists the shaders: path becomes the shader's path from the project.
		bool AcceptShaderDrop(std::string& path);

		// Gives the GameObject the shader, on its Material or a new one.
		void DropShader(GameObject* gameObject, const std::string& path);

		// What the Scripts tab drags: a script type's name, a shader's path
		// from the project, or a folder's path.
		static constexpr const char* script_payload = "LoomScript";
		static constexpr const char* script_folder_payload = "LoomScriptFolder";

		// Takes a script or shader dragged out of the Scripts tab onto a
		// GameObject in the hierarchy, attaching it. Whether the drag was one.
		bool DropScript(GameObject* gameObject);

		// Recompiles the shaders whose files something else has written, every
		// so often.
		void PollShaders();

		// Has the Sprites that show a file the Sprites panel saved draw it as
		// it now is.
		void ReloadSavedSprites();

		// Has the instances of a prefab the Model panel saved take it as it
		// now is.
		void TakeSavedModels();

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

		// Reads <folder>/*.loomproject, if there is one, and hooks up the script
		// library it names.
		void LoadProjectFile();

		// Watches for a finished build and puts the scenes back once the new
		// library is in.
		void PollScripts();

		// Watches for the refresh's build to finish, and closes the editor
		// once it has, unless it failed.
		void PollRefresh();

		// Every owned scene, open shader and sprite, to its file. False when
		// something could not be saved.
		bool SaveProject();

		// SaveActiveScene's choice of file, for any owned scene.
		bool SaveToProject(Scene* scene);

		// Switches updating on, keeping the state the scenes are in now so Stop
		// has something to go back to.
		void BeginPlay();

		// Deletes every open scene here and now, rather than on the task queue:
		// the callers are about to unload the code those scenes are made of.
		void CloseAllScenes();

		// The chosen folder and the scene that was open in it, remembered beside
		// the executable so the prompt is a first-run thing rather than an
		// every-run thing, and so a session picks up where the last one left off.
		void LoadSettings();
		void SaveSettings() const;

		// Writes the frame being drawn to the screenshot's path once it is
		// finished, and ends the run if the screenshot asked to.
		void CaptureFrame();

		// Which of the project's scenes a session opens with.
		std::string StartupScene() const;

		// Selection and the active scene are raw pointers into a hierarchy that
		// edits itself between frames, so both are checked for liveness once per
		// frame rather than trusted.
		void ValidateSelection();

		// Whether the selection is root or under it. False once the selection is
		// gone, which can happen partway through a frame.
		bool IsSelectedIn(const GameObject& root) const;

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

		// Run with the history: carries an edit made in the Prefab panel or to
		// an instance to the file and every instance, and brings instances
		// that do not know their prefab yet into line with it. Only the
		// prefabs of instances changed, of files written and of the panel
		// edited are looked at.
		void SyncPrefabs(const std::unordered_set<GameObject*>& changed);

		// Whether SyncPrefabs has work with no scene changed: a prefab file
		// written from outside, or a Prefab panel edited or still to agree
		// with its file.
		bool PrefabsNeedSync() const;

		// The prefabs read so far whose files were written since.
		std::vector<std::string> ChangedPrefabFiles() const;

		// The Prefab panel's copy made the file's again, in place: the same
		// guids, so the selection and the panel's focus stay where they are.
		void ReshowPrefab();

		// Whether gameObject is in the prefab at path: its scene's copy in the
		// Prefab panel, or an instance of it. A prefab dropped there would
		// take in its own every edit.
		bool IsInPrefab(const GameObject* gameObject, const std::string& path) const;

		// The prefab tree drawn in the Hierarchy, under the open scenes.
		void DrawPrefabNode();

		EditorViewport m_sceneView;
		EditorViewport m_gameView;
		EditorViewport m_debugView;

		EditorCamera m_sceneCamera;
		EditorCamera m_debugCamera;

		SelectionOutline m_sceneOutline;
		SelectionOutline m_debugOutline;

		// The Prefab panel: the prefab open in it, by its path from the project
		// folder, the scene it is shown in, whose first child is the prefab
		// and second the light, and the prefab as the panel last agreed with
		// its file.
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

		// The open prefab, while a build has its scene down.
		std::string m_prefabBeforeBuild{ };

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

		// Shared: unlike an outline, it keeps nothing the size of a view.
		ColliderGizmos m_colliderGizmos;

		ScriptLibrary m_scripts;
		EditorRefresh m_refresh;
		bool m_relaunch = false;
		WebPlayer m_webPlayer;

		std::unique_ptr<EditorFileDialogs> m_dialogs;

		Scene* m_activeScene = nullptr;
		GameObject* m_selected = nullptr;
		uint64_t m_selectedId = 0;
		bool m_nodePressOpened = false;
		std::string m_pendingSelection{ };
		int m_selectionFramesLeft = 0;
		std::vector<Scene*> m_ownedScenes{ };

		// Where each open scene came from, so a scene can be put back after the
		// script library it depends on is rebuilt.
		std::map<Scene*, std::string> m_scenePaths{ };

		std::string m_projectPath{ };
		std::string m_projectFile{ };

		// When PollShaders last looked, in ImGui's seconds.
		double m_shadersPolled = 0.0;
		std::string m_projectName{ };
		std::vector<std::string> m_projectScenes{ };

		// The shaders from the last scan, which the Scripts tab lists.
		std::vector<std::string> m_projectShaders{ };

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

		// Where the Create prompt's script is filed once it is written.
		std::string m_newScriptFolder{ };

		std::vector<FileTab> m_fileTabs{ std::begin(FileTabs::all), std::end(FileTabs::all) };

		// The scene a session opens with: what the settings file said last time,
		// and then whichever one this session last made active.
		std::string m_startupScene{ };

		// The graphics API, font and look, edited in the Settings window.
		EditorSettings m_settings;

		// One entry per scene that was open when a rebuild started, and what a
		// Stop pressed during that rebuild leaves behind for it to come back to.
		std::vector<SceneSnapshot> m_snapshots{ };
		Guid m_selectedBeforeBuild{ };
		bool m_playAfterCompile = false;

		// The scenes as they were when Play was pressed, held for as long as the
		// play session lasts: a paused session is still one Stop away from the
		// state it started in. Play sets the flag rather than the editor reading
		// it off the snapshots, which are empty for a session started with no
		// scene open.
		bool m_playing = false;
		std::vector<SceneSnapshot> m_playSnapshots{ };

		SceneHistory m_history{ };

		// Set when a click lands or a widget is let go of in a window that
		// CanEditScenes, or a scene arrives; a step is recorded on the first
		// frame after that with nothing held down.
		bool m_historyDirty = true;

		// Whether a view's camera is being flown this frame or was just let go
		// of. That edits nothing, so it marks nothing, since comparing a large
		// scene takes long enough to stall the editor.
		bool m_flyingView = false;

		bool m_showProject = true;
		bool m_focusProject = false;

		// The Project panel's tab in front, and the one to bring forward on
		// the next frame, or none: each one of ProjectTabs.
		std::string_view m_projectTab = ProjectTabs::Scenes;
		std::string_view m_projectTabToShow{ };

		bool m_showHierarchy = true;
		bool m_hierarchyFocused = false;
		bool m_showInspector = true;
		bool m_showScene = true;
		bool m_showGame = true;
		bool m_showDebug = true;
		bool m_gamePanelFocused = false;
		bool m_gamePanelHovered = false;
		GameFocus m_gameFocus;
		std::optional<View> m_fullscreen;
		View m_lastView = View::Game;
		WindowFullscreen m_windowFullscreen;
		double m_fullscreenSince = 0.0;
		bool m_showSceneData = true;
		bool m_showConsole = true;
		bool m_showShaders = false;
		bool m_showSprites = false;
		bool m_showModels = false;
		bool m_showPalette = false;
		bool m_showPrefab = false;
		bool m_showStats = true;
		bool m_showKeyBindings = true;
		bool m_showImGuiDemo = false;
		bool m_showSettings = false;

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

		static constexpr size_t max_category_length = 128;

		ComponentMenu m_componentMenu;
		char m_categoryBuffer[max_category_length + 1]{ };
		bool m_openAddComponent = false;

		// Set by "Step": updating is switched on for exactly one frame and
		// switched back off at the top of the next one.
		bool m_steppedLastFrame = false;

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

		EditorConsole m_console;
		KeyBindings m_keyBindings;
		ShaderEditor m_shaders;
		SpriteEditor m_sprites;
		ModelEditor m_models;

		char m_nameBuffer[128]{ };
		const GameObject* m_nameBufferOwner = nullptr;

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

		// Frames left in which the Scene view asks for focus.
		int m_sceneFocusFrames = 0;

		std::string m_screenshotPath{ };
		std::string m_screenshotWindow{ };
		int m_screenshotFrame = 0;
		bool m_screenshotExits = false;
		int m_frame = 0;

		int m_sceneCounter = 0;
	};
};
