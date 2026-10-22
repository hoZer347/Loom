// Before anything that brings in GLFW, which defines APIENTRY itself
// when windows.h has not been seen.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shlobj.h>

#include "Editor.h"
#include "EditorMcp.h"
#include "ProjectAssets.h"
#include "ProjectTemplate.h"
#include "SceneSerializer.h"
#include "UiScript.h"

#include "Audio.h"
#include "Engine.h"
#include "OpenGL.h"
#include "Scene.h"

#include <cctype>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

using namespace Loom;


namespace
{
	// IVirtualDesktopManager from shobjidl.h, which only declares it for a
	// Windows 10 target; the engine targets Windows 7.
	MIDL_INTERFACE("a5cd92ff-29be-454c-8d04-d82879fb3f1b")
	DesktopManager : public IUnknown
	{
		virtual HRESULT STDMETHODCALLTYPE IsWindowOnCurrentVirtualDesktop(HWND window, BOOL* on_current) = 0;
		virtual HRESULT STDMETHODCALLTYPE GetWindowDesktopId(HWND window, GUID* desktop) = 0;
		virtual HRESULT STDMETHODCALLTYPE MoveWindowToDesktop(HWND window, REFGUID desktop) = 0;
	};

	constexpr CLSID CLSID_DesktopManager = { 0xaa509086, 0x5ca9, 0x4c25, { 0x8f, 0x95, 0x58, 0x9d, 0x3c, 0x07, 0xb4, 0x8a } };

	struct FileType
	{
		const char* extension;
		const wchar_t* progId;
		const wchar_t* description;
	};

	constexpr FileType FILE_TYPES[] =
	{
		{ ProjectTemplate::extension, L"Loom.Project", L"Loom Project" },
		{ SceneSerializer::extension, L"Loom.Scene", L"Loom Scene" },
		{ ScriptFolders::extension, L"Loom.ScriptFolders", L"Loom Script Folders" },
		{ TwoDFolders::extension, L"Loom.TwoDFolders", L"Loom 2D Folders" },
		{ InputActions::extension, L"Loom.InputActions", L"Loom Key Bindings" },
	};

	// --create-<kind> <name>, for each of these.
	constexpr std::string_view createPrefix = "--create-";

	std::optional<AssetKind> ScriptKindNamed(std::string_view kind)
	{
		constexpr std::pair<std::string_view, AssetKind> kinds[] =
		{
			{ "script", AssetKind::Script },
			{ "state", AssetKind::State },
			{ "state-machine", AssetKind::StateMachine },
		};

		for (const auto& [name, found] : kinds)
			if (kind == name)
				return found;

		return std::nullopt;
	};

	// Makes this executable what opens the editor's files for the current
	// user, so double-clicking one in Explorer opens it here. Per-user, so it
	// needs no elevation; Explorer is only told when something actually changed.
	void ClaimFileTypes()
	{
		wchar_t executable[MAX_PATH]{ };

		const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);

		if (length == 0 || length == MAX_PATH)
			return;

		const std::wstring classes = L"Software\\Classes\\";
		const std::wstring command = L"\"" + std::wstring(executable) + L"\" \"%1\"";

		const auto set = [](const std::wstring& key, const wchar_t* name, const std::wstring& value)
		{
			return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ,
				value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
		};

		bool changed = false;

		for (const FileType& type : FILE_TYPES)
		{
			const std::string narrow = type.extension;
			const std::wstring extension_key = classes + std::wstring(narrow.begin(), narrow.end());
			const std::wstring prog_id_key = classes + type.progId;
			const std::wstring command_key = prog_id_key + L"\\shell\\open\\command";

			std::wstring current(command.size() + 1, L'\0');
			DWORD size = (DWORD)(current.size() * sizeof(wchar_t));

			if (RegGetValueW(HKEY_CURRENT_USER, command_key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, current.data(), &size) == ERROR_SUCCESS &&
				command == current.c_str())
				continue;

			if (set(extension_key, nullptr, type.progId) &&
				set(extension_key + L"\\OpenWithProgids", type.progId, L"") &&
				set(prog_id_key, nullptr, type.description) &&
				set(command_key, nullptr, command))
				changed = true;
			else
				std::cerr << "Could not register " << narrow << " files with Explorer" << std::endl;
		};

		if (changed)
			SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
	};

	void Usage()
	{
		std::cout
			<< "Loom Editor [project folder, .loomproject or .loomscene]\n"
			<< "  --new-project <folder> <name>  create a project and open it\n"
			<< "  --new-script <scripts project> <folder> <name>\n"
			<< "                                 write a script into that folder and exit,\n"
			<< "                                 printing its path\n"
			<< "  --create-script <name>         Create > Script in the project, adding a\n"
			<< "                                 scripts project if it has none\n"
			<< "  --create-state <name>          the same for Create > State\n"
			<< "  --create-state-machine <name>  and for Create > State Machine\n"
			<< "  --create-prompt <kind>         start with the Create prompt open for a\n"
			<< "                                 script, state or state-machine\n"
			<< "  --compile                      build the project's scripts on startup\n"
			<< "  --play                         start with the scene running\n"
			<< "  --play-web                     build the scene for the web and open it\n"
			<< "  --import <file> [x y]          import a model, as if dropped on the window;\n"
			<< "                                 over the Project or 2D panel, copy the file there\n"
			<< "  --preview <file>               select a model file, showing it in the Inspector\n"
			<< "  --select <name>                select the first GameObject called that\n"
			<< "  --open-project-dialog          start with File > Open Project... open\n"
			<< "  --settings                     start with the Settings window open\n"
			<< "  --open-demo <name>             open a project from the engine's Demos folder\n"
			<< "  --open-scene <file>            open a scene beside those already open, even\n"
			<< "                                 one of them again\n"
			<< "  --open-shader <file>           open a shader in the Shaders panel\n"
			<< "  --layout <name>                load a layout saved from the Layout menu\n"
			<< "  --save-layout <name>           save the layout once the panels are drawn\n"
			<< "  --save-layout-prompt           start with Layout > Save Layout... open\n"
			<< "  --open-sprite <file>           open an image in the Sprites panel\n"
			<< "  --open-model <prefab>          open a prefab in the Model panel\n"
			<< "  --add-component-prompt         start with Add Component open on the selection\n"
			<< "  --set-component-category <component> <category>\n"
			<< "                                 file a component under a category in the\n"
			<< "                                 project, as right-clicking it in Add\n"
			<< "                                 Component does; \"\" puts it back\n"
			<< "  --undo, --redo                 step the history once the imports have landed\n"
			<< "  --click <x y> [x y]            left click there once the scripts are built,\n"
			<< "                                 letting go at the second point if given\n"
			<< "  --vulkan, --opengl             run on that graphics API this time\n"
			<< "  --screenshot <file> [frames]   write the editor to a bitmap and exit,\n"
			<< "                                 without showing a window\n"
			<< "  --ui-script <file>             click, type and screenshot through the UI\n"
			<< "                                 from a script (see UiScript.h), then exit,\n"
			<< "                                 without showing a window\n"
			<< "  --desktop <id>                 show the window on that virtual desktop,\n"
			<< "                                 without activating it\n"
			<< "  --mcp <port>                   serve the editor's MCP tools on that port; a\n"
			<< "                                 run with a window serves them on "
			<< EditorMcp::default_port << " unasked"
			<< std::endl;
	};

	// An automated run has nobody to answer a crash dialog, which would come up
	// on the screen of whoever is at the machine and hold the run until they
	// did. abort(), failed asserts and the debug runtime's errors are written
	// to stderr instead, and a crash ends the process without Windows Error
	// Reporting's dialog.
	void ReportCrashesToStderr()
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		_set_error_mode(_OUT_TO_STDERR);
		_set_abort_behavior(0, _CALL_REPORTFAULT);

		for (const int report : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
		{
			_CrtSetReportMode(report, _CRTDBG_MODE_FILE);
			_CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
		};
	};

	// Puts the (still hidden) window on another virtual desktop and shows it
	// there without activating it, so it never takes the focus. The shell only
	// tracks a window on a desktop once it has been shown, so it is shown
	// cloaked first and uncloaked after the move.
	bool ShowOnDesktop(GLFWwindow* window, const std::string& id)
	{
		const std::wstring braced = L"{" + std::wstring(id.begin(), id.end()) + L"}";

		GUID desktop{ };

		if (FAILED(CLSIDFromString(braced.c_str(), &desktop)))
		{
			std::cerr << "--desktop: " << id << " is not a desktop id" << std::endl;
			return false;
		};

		const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

		DesktopManager* manager = nullptr;
		HRESULT result = CoCreateInstance(CLSID_DesktopManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&manager));

		const HWND hwnd = glfwGetWin32Window(window);
		BOOL cloak = TRUE;

		DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
		ShowWindow(hwnd, SW_SHOWNOACTIVATE);

		if (SUCCEEDED(result))
		{
			result = manager->MoveWindowToDesktop(hwnd, desktop);
			manager->Release();
		};

		if (SUCCEEDED(com))
			CoUninitialize();

		if (FAILED(result))
		{
			std::cerr << "--desktop: could not move the window to " << id << std::endl;
			ShowWindow(hwnd, SW_HIDE);
			return false;
		};

		cloak = FALSE;
		DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));

		return true;
	};
};

// With a project folder given, the editor starts on it. Without one it uses
// whichever folder it was pointed at last, and asks for one when there is no
// answer to that either.
int main(int argc, char** argv)
{
	// The editor is a Windows program, so it opens no console of its own and
	// its Console panel is where it talks. Started from a terminal, what it
	// says goes there too, unless that was redirected already.
	if ((_fileno(stdout) < 0 || _fileno(stderr) < 0) && AttachConsole(ATTACH_PARENT_PROCESS))
	{
		FILE* reopened = nullptr;

		if (_fileno(stdout) < 0)
			freopen_s(&reopened, "CONOUT$", "w", stdout);

		if (_fileno(stderr) < 0)
			freopen_s(&reopened, "CONOUT$", "w", stderr);
	};

	// Visual Studio's Add > Loom Script runs this, so it is over before there is
	// an engine or a window.
	for (int i = 1; i < argc; i++)
		if (std::string(argv[i]) == "--new-script")
		{
			if (i + 3 >= argc)
			{
				std::cerr << "--new-script needs the scripts project, a folder and a name" << std::endl;
				return 1;
			};

			std::string error;
			const std::string path = ProjectAssets::CreateScript(
				argv[i + 1],
				argv[i + 2],
				argv[i + 3],
				ProjectAssets::ScriptKind::Component,
				&error);

			if (path.empty())
			{
				std::cerr << error << std::endl;
				return 1;
			};

			std::cout << path << std::endl;
			return 0;
		};

	// A screenshot or UI script run is automated by definition: it renders,
	// writes its bitmaps and exits, so it does that without putting a window
	// on screen.
	// Read before the Engine, which is what opens the window.
	//
	// A run on another virtual desktop starts hidden too, and is shown once it
	// is over there.
	std::string desktop;

	Engine::backend = Editor::SavedBackend();
	Engine::vsync = false;

	for (int i = 1; i < argc; i++)
	{
		const std::string argument = argv[i];

		if (argument == "--vulkan")
			Engine::backend = Backend::Vulkan;

		if (argument == "--opengl")
			Engine::backend = Backend::OpenGL;

		if (argument == "--screenshot" || argument == "--ui-script" || argument == "--desktop")
			Engine::showWindow = false;

		if (argument == "--desktop" && i + 1 < argc)
			desktop = argv[i + 1];
	};

	if (!Engine::showWindow)
		ReportCrashesToStderr();

	// Nobody is listening to an automated run either, and its sounds would
	// play over whatever whoever is at the machine is doing.
	Audio::useDevice = Engine::showWindow;

	// A refresh starts the new editor once this one is gone, its MCP port
	// freed and what it writes as it closes written.
	std::filesystem::path relaunch;
	std::string relaunch_arguments;
	bool failed = false;

	{
		// The Engine comes first: it owns the window, the renderer and the ImGui
		// context the editor draws itself into.
		Engine engine;

		// An automated run is often a scratch build that is deleted afterwards,
		// which must not take the file association with it.
		if (Engine::showWindow)
			ClaimFileTypes();

		glfwSetWindowTitle(Engine::window, "Loom Editor");
		glfwSetWindowSize(Engine::window, 1600, 900);

		if (!desktop.empty() && !ShowOnDesktop(Engine::window, desktop))
			return 1;

		// Opening a project moves the working directory into it, and the editor
		// opens the last one as it starts, so relative paths on the command line
		// are taken from where it was started.
		const std::filesystem::path launch_folder = std::filesystem::current_path();

		Editor editor;

		bool compile = false;
		bool play = false;
		bool play_web = false;
		bool open_project_dialog = false;
		bool settings = false;
		std::string screenshot;
		std::string preview;
		std::string selection;
		std::string new_script;
		AssetKind new_script_kind = AssetKind::Script;
		std::vector<std::pair<std::string, std::string>> categories;
		std::optional<AssetKind> create_prompt;
		int frames = 240;

		// After the Engine, whose ImGui context it listens to.
		std::unique_ptr<UiScript> script;
		bool exit_after_script = false;

		unsigned short mcp_port = Engine::showWindow ? EditorMcp::default_port : 0;

		for (int i = 1; i < argc; i++)
		{
			const std::string argument = argv[i];

			if (argument == "--help" || argument == "-h")
			{
				Usage();
			}
			else if (argument == "--new-project")
			{
				if (i + 2 >= argc)
				{
					std::cerr << "--new-project needs a folder and a name" << std::endl;
					return 1;
				};

				if (!editor.CreateProject(argv[i + 1], argv[i + 2]))
					return 1;

				i += 2;
			}
			else if (argument == "--screenshot")
			{
				if (i + 1 >= argc)
				{
					std::cerr << "--screenshot needs a file to write" << std::endl;
					return 1;
				};

				screenshot = argv[++i];

				// An optional frame count: enough of them for a build to finish and
				// for the scene to have been ticked.
				if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0]))
					frames = atoi(argv[++i]);
			}
			else if (argument == "--ui-script")
			{
				if (i + 1 >= argc)
				{
					std::cerr << "--ui-script needs a script file" << std::endl;
					return 1;
				};

				script = std::make_unique<UiScript>(launch_folder);
				exit_after_script = true;

				if (!script->Load(argv[++i]))
					return 1;
			}
			else if (argument == "--mcp")
			{
				constexpr long highest_port = std::numeric_limits<unsigned short>::max();

				char* end = nullptr;
				const long port = i + 1 < argc ? strtol(argv[i + 1], &end, 10) : 0;

				if (i + 1 >= argc || *end != '\0' || port < 1 || port > highest_port)
				{
					std::cerr << "--mcp needs a port from 1 to " << highest_port << std::endl;
					return 1;
				};

				mcp_port = (unsigned short)port;
				i++;
			}
			else if (argument == "--import")
			{
				if (i + 1 >= argc)
				{
					std::cerr << "--import needs a model file" << std::endl;
					return 1;
				};

				const std::string path = argv[++i];

				// An optional drop position, which picks the parent the way the
				// mouse does when a file is dragged in.
				if (i + 2 < argc && isdigit((unsigned char)argv[i + 1][0]) && isdigit((unsigned char)argv[i + 2][0]))
				{
					editor.Drop(path, (float)atof(argv[i + 1]), (float)atof(argv[i + 2]));
					i += 2;
				}
				else editor.QueueImport(path);
			}
			else if (argument == "--preview")
			{
				if (i + 1 >= argc)
				{
					std::cerr << "--preview needs a model file" << std::endl;
					return 1;
				};

				preview = argv[++i];
			}
			else if (argument == "--create-prompt")
			{
				if (i + 1 >= argc || !ScriptKindNamed(argv[i + 1]))
				{
					std::cerr << "--create-prompt needs script, state or state-machine" << std::endl;
					return 1;
				};

				create_prompt = ScriptKindNamed(argv[++i]);
			}
			else if (argument.starts_with(createPrefix) && ScriptKindNamed(argument.substr(createPrefix.size())))
			{
				if (i + 1 >= argc)
				{
					std::cerr << argument << " needs a name" << std::endl;
					return 1;
				};

				new_script_kind = *ScriptKindNamed(argument.substr(createPrefix.size()));
				new_script = argv[++i];
			}
			else if (argument == "--set-component-category")
			{
				if (i + 2 >= argc)
				{
					std::cerr << "--set-component-category needs a component and a category" << std::endl;
					return 1;
				};

				categories.emplace_back(argv[i + 1], argv[i + 2]);
				i += 2;
			}
			else if (argument == "--select")
			{
				if (i + 1 >= argc)
				{
					std::cerr << "--select needs a GameObject name" << std::endl;
					return 1;
				};

				selection = argv[++i];
			}
			else if (argument == "--desktop")
			{
				if (i + 1 >= argc)
				{
					std::cerr << "--desktop needs a virtual desktop id" << std::endl;
					return 1;
				};

				i++;
			}
			else if (argument == "--vulkan" || argument == "--opengl")
				continue;
			else if (argument == "--compile")
				compile = true;
			else if (argument == "--play")
				play = true;
			else if (argument == "--play-web")
				play_web = true;
			else if (argument == "--open-project-dialog")
				open_project_dialog = true;
			else if (argument == "--settings")
				settings = true;
			else if (argument == "--open-scene")
			{
				if (i + 1 >= argc)
				{
					std::cerr << "--open-scene needs a scene file" << std::endl;
					return 1;
				};

				editor.OpenScene(argv[++i]);
			}
			else if (argument == "--open-shader")
			{
				if (i + 1 >= argc)
				{
					std::cerr << "--open-shader needs a shader file" << std::endl;
					return 1;
				};

				editor.OpenShader(argv[++i]);
			}
			else if (argument == "--layout" || argument == "--save-layout")
			{
				if (i + 1 >= argc)
				{
					std::cerr << argument << " needs a layout name" << std::endl;
					return 1;
				};

				if (argument == "--layout")
					editor.LoadLayout(argv[++i]);
				else
					editor.SaveLayout(argv[++i]);
			}
			else if (argument == "--save-layout-prompt")
				editor.ShowSaveLayoutPrompt();
			else if (argument == "--open-sprite")
			{
				if (i + 1 >= argc)
				{
					std::cerr << "--open-sprite needs an image file" << std::endl;
					return 1;
				};

				editor.OpenSprite(argv[++i]);
			}
			else if (argument == "--open-model")
			{
				if (i + 1 >= argc)
				{
					std::cerr << "--open-model needs a prefab" << std::endl;
					return 1;
				};

				editor.OpenModel(argv[++i]);
			}
			else if (argument == "--add-component-prompt")
				editor.ShowAddComponent();
			else if (argument == "--open-demo")
			{
				if (i + 1 >= argc)
				{
					std::cerr << "--open-demo needs a demo name" << std::endl;
					return 1;
				};

				editor.OpenDemo(argv[++i]);
			}
			else if (argument == "--click")
			{
				if (i + 2 >= argc)
				{
					std::cerr << "--click needs a point" << std::endl;
					return 1;
				};

				const float x = (float)atof(argv[++i]);
				const float y = (float)atof(argv[++i]);

				// An optional point to let go at, which makes it a drag.
				if (i + 2 < argc && isdigit((unsigned char)argv[i + 1][0]) && isdigit((unsigned char)argv[i + 2][0]))
				{
					editor.QueueClick(x, y, (float)atof(argv[i + 1]), (float)atof(argv[i + 2]));
					i += 2;
				}
				else editor.QueueClick(x, y, x, y);
			}
			else if (argument == "--undo" || argument == "--redo")
				editor.QueueHistoryStep(argument == "--undo");
			else if (argument.rfind("--", 0) == 0)
			{
				std::cerr << "Unknown option " << argument << std::endl;
				Usage();
				return 1;
			}
			else editor.OpenProject((launch_folder / argument).string());
		};

		if (!new_script.empty())
			editor.CreateScriptAsset(new_script_kind, new_script);

		for (const auto& [component, category] : categories)
			editor.SetComponentCategory(component, category);

		if (compile)
			editor.CompileScripts();

		// The same path the Play button takes, so the scripts are compiled first
		// here too.
		if (play)
			editor.TogglePlay();

		if (play_web)
			editor.PlayWeb();

		if (open_project_dialog)
			editor.ShowOpenProjectDialog();

		if (settings)
			editor.ShowSettings();

		if (create_prompt)
			editor.AskForAsset(*create_prompt, editor.GetProjectPath());

		// After the project is open, since opening its scene selects that instead.
		if (!preview.empty())
			editor.SelectModel(preview);

		if (!selection.empty())
			editor.RequestSelection(selection);

		if (!screenshot.empty())
			editor.RequestScreenshot(screenshot, frames, true);

		// The server's ui tool runs its commands through the same UiScript as a
		// --ui-script run, since only one can listen to ImGui's items.
		std::unique_ptr<EditorMcp> mcp;

		if (mcp_port != 0)
		{
			if (!script)
			{
				script = std::make_unique<UiScript>(launch_folder);
				script->Listen(false);
			};

			mcp = std::make_unique<EditorMcp>(editor, *script);

			if (!mcp->Listen(mcp_port))
			{
				mcp.reset();

				if (!exit_after_script)
					script.reset();
			};
		};

		if (script)
			Engine::SetImGuiInputFunction(
				[&script, &mcp, exit_after_script]()
				{
					script->Step();

					if (mcp)
						mcp->Step();

					if (exit_after_script && script->Finished())
						Engine::Stop();
				});

		engine.Start();

		if (editor.WantsRelaunch())
		{
			std::string options = Engine::backend == Backend::Vulkan ? "--vulkan" : "--opengl";

			if (mcp_port != 0 && mcp_port != EditorMcp::default_port)
				options += " --mcp " + std::to_string(mcp_port);

			if (!desktop.empty())
				options += " --desktop " + desktop;

			if (const std::string target = editor.RelaunchTarget(); !target.empty())
				options += " \"" + target + '"';

			relaunch = editor.RelaunchExecutable();
			relaunch_arguments = options;
		};

		failed = exit_after_script && script->Failed();
	};

	if (!relaunch.empty() && !EditorRefresh::Relaunch(relaunch, relaunch_arguments))
		return 1;

	return failed ? 1 : 0;
};
