#pragma once

#include "EditorViewport.h"
#include "Guid.h"
#include "EditorSettings.h"
#include "ModelPreview.h"
#include "SceneHistory.h"
#include "ScriptLibrary.h"
#include "WebPlayer.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ImGuiContext;


namespace Loom
{
	struct Scene;
	struct GameObject;
	struct ComponentBase;
	struct LoomObject;

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
	};

	enum class AssetKind
	{
		Folder,
		Scene,
		Script,
		Shader,
		Texture,
	};

	/**
	* Loom::Editor
	* - A Unity-style shell around the Loom runtime
	* - Owns one dock space holding the scene hierarchy, an inspector, a console
	*   and, in the centre, whichever scene is selected, rendered into an
	*   off-screen target so it sits inside the layout like any other panel
	* - Works out of a project folder: everything under it is listed in the
	*   Project panel, which also creates new assets, and the scripts are a C++
	*   project that builds into a library the editor loads
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
		// closed from the File menu; scenes created anywhere else still show up
		// in the hierarchy, they just cannot be closed from it.
		Scene* CreateScene(const std::string& name = "New Scene");
		Scene* OpenScene(const std::string& path);
		bool SaveScene(Scene* scene, const std::string& path);
		void CloseScene(Scene* scene);

		// The folder the editor works out of. Everything under it is listed in
		// the Project panel; the choice is remembered between runs, so
		// the folder prompt only shows up when there is nothing to remember.
		// A .loomproject file opens the folder it is in, and a .loomscene file
		// opens its project on that scene.
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
		// without a visible window.
		void RequestScreenshot(const std::string& path, int after_frames, bool exit_after);

		// Imports a model file once the next frame has drawn the panels, into
		// whatever the mouse is over: a Hierarchy node, the Scene view, or
		// failing those the selection.
		void QueueImport(const std::string& path);

		// The same, with the mouse moved to x, y first, in ImGui's coordinates.
		void Drop(const std::string& path, float x, float y);

		// File > Open Scene..., starting beside the scene it last opened.
		void ShowOpenSceneDialog();

		// Window > Settings.
		void ShowSettings() { m_showSettings = true; };

		void SetActiveScene(Scene* scene);

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

	private:
		void DrawGui();

		void DrawMainMenuBar();
		void DrawProject();
		void DrawAssetNode(const AssetNode& node, int depth);
		void DrawAssetContextMenu(const AssetNode& node);
		void DrawCreateMenu(const std::string& folder);
		void DrawCreateAssetPrompt();
		void DrawScriptsSection();
		void DrawHierarchy();
		void DrawSceneNode(Scene* scene);
		void DrawGameObjectNode(GameObject* gameObject);

		// Marks every node that has gained a child since the last frame, and the
		// nodes above it, to be drawn open.
		void ExpandGrownNodes(const std::vector<Scene*>& scenes);

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
		void DrawFields(LoomObject& object, const std::vector<std::string>& labels, float column);
		void DrawModelPreview();
		void DrawScene();
		void DrawSceneData();
		void DrawConsole();
		void DrawStats();

		// The first-run "where are your scenes?" prompt, the new project dialog,
		// and the browsers behind every Browse... button.
		void DrawFolderPrompt();
		void DrawNewProjectPrompt();
		void DrawFileDialogs();

		void HandleShortcuts();

		void RecordHistory();
		bool CanStepHistory() const;

		// Swaps an owned scene for one built from text, in the same place: its
		// file, whether it is active, and its slot among the owned scenes.
		Scene* ReplaceScene(Scene* scene, const std::string& text);

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

		void BuildDefaultLayout(unsigned int dockspace_id);

		// Records the item just drawn as where a queued import goes, if the
		// mouse is over it.
		void TakeImportTarget(GameObject* gameObject);
		void ImportQueued();

		// Rescans the project folder: the asset tree the Project panel draws,
		// and the scenes in it.
		void RefreshProjectAssets();

		// Scenes open in the editor; anything else goes to whatever Windows
		// opens that kind of file with, and to the text editor when nothing
		// claims it.
		void OpenAsset(const std::string& path);

		// ImGui's way of opening a link, which for a file in the project is
		// OpenAsset's.
		static bool OpenInShell(ImGuiContext* context, const char* path);

		// Lets a file be dragged out of the Project panel, onto a field that
		// names a file or a GameObject in the hierarchy.
		void DragAsset(const AssetNode& node) const;

		// Gives the GameObject the shader, on its Material or a new one. Whether
		// the drop was taken.
		bool DropShader(GameObject* gameObject);

		// Recompiles the shaders whose files something else has written, every
		// so often.
		void PollShaders();

		// Where the Create prompt's asset would be written: the file for most,
		// the source for a script, which goes under the scripts project even
		// when asked for somewhere else.
		std::string NewAssetPath() const;
		std::string NewAssetFolder() const;

		// Why the Create prompt's name cannot be used, or an empty string.
		std::string NewAssetProblem() const;
		void CreateAsset();

		// Points a change notification at the project folder, so the tree is
		// rescanned when something under it is added, removed or renamed
		// rather than on a timer.
		void WatchProject();
		bool ProjectChanged();

		// Reads <folder>/*.loomproject, if there is one, and hooks up the script
		// library it names.
		void LoadProjectFile();

		// Watches for a finished build and puts the scenes back once the new
		// library is in.
		void PollScripts();

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
		static bool IsInHierarchy(const GameObject& root, const GameObject* target);

		bool IsOwned(Scene* scene) const;

		// Where the given scene would be written if it were saved right now.
		std::string DefaultPathFor(Scene* scene) const;

		EditorViewport m_viewport;
		ScriptLibrary m_scripts;
		WebPlayer m_webPlayer;

		std::unique_ptr<EditorFileDialogs> m_dialogs;

		Scene* m_activeScene = nullptr;
		GameObject* m_selected = nullptr;
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

		// A model file picked in the Project panel, previewed in the Inspector
		// in place of a GameObject.
		std::string m_selectedModel{ };
		ModelPreview m_modelPreview;

		// The project as it stood at the last scan.
		AssetNode m_assets{ };
		void* m_assetWatch = nullptr;
		std::string m_assetWatchPath{ };
		std::string m_selectedAsset{ };
		bool m_revealSelectedAsset = false;

		// How tall the scripts section under the tree came out last frame.
		float m_scriptsSectionHeight = 0.0f;

		// The scene a session opens with: what the settings file said last time,
		// and then whichever one this session last made active.
		std::string m_startupScene{ };

		// What the Open Scene dialog last picked, so it reopens beside it.
		std::string m_lastOpenedScene{ };

		// The graphics API, font and look, edited in the Settings window.
		EditorSettings m_settings;

		// One entry per scene that was open when a rebuild started, and what a
		// Stop pressed during that rebuild leaves behind for it to come back to.
		std::vector<SceneSnapshot> m_snapshots{ };
		bool m_playAfterCompile = false;

		// The scenes as they were when Play was pressed, held for as long as the
		// play session lasts: a paused session is still one Stop away from the
		// state it started in. Play sets the flag rather than the editor reading
		// it off the snapshots, which are empty for a session started with no
		// scene open.
		bool m_playing = false;
		std::vector<SceneSnapshot> m_playSnapshots{ };

		SceneHistory m_history{ };

		// Set when a click lands, a widget is let go of or a scene arrives; the
		// scenes are compared on the first frame after that with nothing held down.
		bool m_historyDirty = true;

		bool m_showProject = true;
		bool m_showHierarchy = true;
		bool m_hierarchyFocused = false;
		bool m_showInspector = true;
		bool m_showScene = true;
		bool m_showSceneData = true;
		bool m_showConsole = true;
		bool m_showStats = true;
		bool m_showImGuiDemo = false;
		bool m_showSettings = false;

		bool m_firstFrame = true;
		bool m_rebuildLayout = false;

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

		bool m_consoleAutoScroll = true;
		char m_consoleFilter[128]{ };

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

		// Whether the mouse has been put where the front drop happened, so the
		// panels drawn since have hovered what it lands in.
		bool m_importPlaced = false;

		// True for an undo, false for a redo.
		std::vector<bool> m_historySteps{ };

		// By GUID rather than pointer, here and in the Hierarchy's tree ids:
		// undo rebuilds a scene into new objects that keep their GUIDs.
		std::unordered_map<Guid, size_t> m_childCounts{ };
		std::unordered_set<Guid> m_expand{ };
		// Double-clicking the selection in the hierarchy edits its name in place,
		// in the same buffer as the inspector's name box.
		bool m_renaming = false;
		bool m_focusRename = false;

		std::string m_screenshotPath{ };
		int m_screenshotFrame = 0;
		bool m_screenshotExits = false;
		int m_frame = 0;

		int m_sceneCounter = 0;
	};
};
