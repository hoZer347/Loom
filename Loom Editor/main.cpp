#ifdef _WIN32
// Before GLFW, which defines APIENTRY itself when windows.h has not been seen.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <objbase.h>
#endif

#include "CrashReports.h"
#include "Editor.h"
#include "EditorMcp.h"
#include "FileTypes.h"
#include "ProjectAssets.h"
#include "UiScript.h"
#include "VisualStudio.h"

#include "Audio.h"
#include "Engine.h"
#include "OpenGL.h"
#include "Scene.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#pragma comment(lib, "dwmapi.lib")
#else
#include <GLFW/glfw3.h>
#endif

using namespace Loom;


namespace
{
	#pragma region Desktop

#ifdef _WIN32
	// shobjidl.h only declares it for a Windows 10 target; the engine targets Windows 7.
	MIDL_INTERFACE("a5cd92ff-29be-454c-8d04-d82879fb3f1b")
	DesktopManager : public IUnknown
	{
		virtual HRESULT STDMETHODCALLTYPE IsWindowOnCurrentVirtualDesktop(HWND window, BOOL* on_current) = 0;
		virtual HRESULT STDMETHODCALLTYPE GetWindowDesktopId(HWND window, GUID* desktop) = 0;
		virtual HRESULT STDMETHODCALLTYPE MoveWindowToDesktop(HWND window, REFGUID desktop) = 0;
	};

	constexpr CLSID CLSID_DesktopManager = { 0xaa509086, 0x5ca9, 0x4c25, { 0x8f, 0x95, 0x58, 0x9d, 0x3c, 0x07, 0xb4, 0x8a } };

	// The shell only tracks a shown window, so it is shown cloaked, moved, then uncloaked.
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
#else
	// Virtual desktops by id are the Windows shell's.
	bool ShowOnDesktop(GLFWwindow*, const std::string&)
	{
		std::cerr << "--desktop: virtual desktops are only on Windows" << std::endl;
		return false;
	};
#endif

	#pragma endregion

	#pragma region File Types

	// False when this is not the main checkout's editor, or there is no Explorer to claim them in.
	bool ClaimFileTypes()
	{
#ifdef _WIN32
		wchar_t executable[MAX_PATH]{ };

		const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);

		if (length == 0 || length == MAX_PATH)
			return false;

		return FileTypes::Claim(executable);
#else
		return false;
#endif
	};

	#pragma endregion

	#pragma region Options

	// --create-<kind> <name>, for each of these.
	constexpr std::string_view createPrefix = "--create-";

	std::optional<AssetKind> ScriptKindNamed(std::string_view kind)
	{
		constexpr std::pair<std::string_view, AssetKind> kinds[] =
		{
			{ "script", AssetKind::Script },
			{ "state", AssetKind::State },
			{ "state-machine", AssetKind::StateMachine },
			{ "dialogue-manager", AssetKind::DialogueManager },
			{ "test", AssetKind::Test },
		};

		for (const auto& [name, found] : kinds)
			if (kind == name)
				return found;

		return std::nullopt;
	};

	void Usage()
	{
		std::cout
			<< "Loom Editor [project folder or one of its .loom files]\n"
			<< "Loom Editor <header.hpp>         open it in Visual Studio on the nearest\n"
			<< "                                 solution above it, and exit\n"
			<< "  --new-project <folder> <name>  create a project and open it\n"
			<< "  --new-script <scripts project> <folder> <name>\n"
			<< "                                 write a script into that folder and exit,\n"
			<< "                                 printing its path\n"
			<< "  --create-script <name>         Create > Script in the project, adding a\n"
			<< "                                 scripts project if it has none\n"
			<< "  --create-state <name>          the same for Create > State\n"
			<< "  --create-state-machine <name>  the same for Create > State Machine\n"
			<< "  --create-dialogue-manager <name>\n"
			<< "                                 and for Create > Dialogue Manager\n"
			<< "  --create-test <name>           and for Create > Test\n"
			<< "  --create-prompt <kind>         start with the Create prompt open for a\n"
			<< "                                 script, state, state-machine, dialogue-manager\n"
			<< "                                 or test\n"
			<< "  --compile                      build the project's scripts on startup\n"
			<< "  --log-scripts-project          print each scripts project opened\n"
			<< "  --claim-file-types             make this editor what opens .loom files in\n"
			<< "                                 Explorer, as showing its window does, and\n"
			<< "                                 exit; fails for a worktree's editor\n"
			<< "  --play                         start with the scene running\n"
			<< "  --play-web                     build the scene for the web and open it\n"
			<< "  --import <file> [x y]          import a model, as if dropped on the window;\n"
			<< "                                 over the Project or an asset panel, copy the\n"
			<< "                                 file there\n"
			<< "  --preview <file>               select a model file, showing it in the Inspector\n"
			<< "  --select <name>                select the first GameObject called that;\n"
			<< "                                 again to add another to the selection\n"
			<< "  --open-project-dialog          start with File > Open Project... open\n"
			<< "  --settings                     start with the Settings window open\n"
			<< "  --open-demo <name>             open a project from the engine's Demos folder\n"
			<< "  --open-scene <file>            open a scene beside those already open, even\n"
			<< "                                 one of them again\n"
			<< "  --open-shader <file>           open a shader in the Shaders panel\n"
			<< "  --layout <name>                load a layout saved from the Layout menu\n"
			<< "  --save-layout <name>           save the layout once the panels are drawn\n"
			<< "  --save-layout-prompt           start with Layout > Save Layout... open\n"
			<< "  --open-sprite <file>           open an image or sprite in the Sprites panel\n"
			<< "  --open-model <prefab|model>    open a prefab in the Model panel, or a model\n"
			<< "                                 as the prefab named after it beside it\n"
			<< "  --add-component-prompt         start with Add Component open on the selection\n"
			<< "  --set-component-category <component> <category>\n"
			<< "                                 file a component under a category in the\n"
			<< "                                 project, as right-clicking it in Add\n"
			<< "                                 Component does; \"\" puts it back\n"
			<< "  --undo, --redo                 step the history once the imports have landed\n"
			<< "  --click <x y> [x y]            left click there once the scripts are built,\n"
			<< "                                 letting go at the second point if given\n"
			<< "  --run-tests [filter]           run the tests whose names hold filter, or\n"
			<< "                                 every test, compiling stale scripts first\n"
			<< "  --vulkan, --opengl             run on that graphics API this time\n"
			<< "  --screenshot <file> [frames]   write the editor to a bitmap and exit,\n"
			<< "                                 without showing a window\n"
			<< "  --screenshot-window <name>     only the first shown window whose name\n"
			<< "                                 contains name, such as Inspector\n"
			<< "  --ui-script <file>             click, type and screenshot through the UI\n"
			<< "                                 from a script (see UiScript.h), then exit,\n"
			<< "                                 without showing a window\n"
			<< "  --desktop <id>                 show the window on that virtual desktop,\n"
			<< "                                 without activating it\n"
			<< "  --mcp <port>                   serve the editor's MCP tools on that port,\n"
			<< "                                 shared by every editor given it; a run\n"
			<< "                                 with a window serves them on "
			<< EditorMcp::default_port << " unasked"
			<< std::endl;
	};

	#pragma endregion

};

int main(int argc, char** argv)
{
#ifdef _WIN32
	// A Windows program has no console; started from a terminal it writes there too.
	if ((_fileno(stdout) < 0 || _fileno(stderr) < 0) && AttachConsole(ATTACH_PARENT_PROCESS))
	{
		FILE* reopened = nullptr;

		if (_fileno(stdout) < 0)
			freopen_s(&reopened, "CONOUT$", "w", stdout);

		if (_fileno(stderr) < 0)
			freopen_s(&reopened, "CONOUT$", "w", stderr);
	};
#endif

	// Visual Studio's Add > Loom Script runs this before there is an engine or window.
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

	// Explorer sends a double-clicked header here, to open in its project's Visual Studio.
	if (argc == 2 && VisualStudio::IsHeader(argv[1]))
	{
		// Explorer gives no console, so what went wrong shows in a box.
		std::ostringstream errors;
		std::streambuf* const console = std::cerr.rdbuf(errors.rdbuf());

		if (std::thread opening = VisualStudio::Open(VisualStudio::SolutionFor(argv[1]), argv[1]); opening.joinable())
			opening.join();

		std::cerr.rdbuf(console);

		if (errors.str().empty())
			return 0;

		std::cerr << errors.str();
#ifdef _WIN32
		MessageBoxA(nullptr, errors.str().c_str(), "Loom", MB_OK | MB_ICONERROR);
#endif

		return 1;
	};

	// Automated runs and runs on another desktop start hidden; read before the Engine opens the window.
	std::string desktop;

	Engine::backend = Editor::SavedBackend();
	Engine::vsync = false;

	for (int i = 1; i < argc; i++)
	{
		const std::string argument = argv[i];

		if (argument == "--claim-file-types")
			return ClaimFileTypes() ? 0 : 1;

		if (argument == "--vulkan")
			Engine::backend = Backend::Vulkan;

		if (argument == "--opengl")
			Engine::backend = Backend::OpenGL;

		if (argument == "--screenshot" || argument == "--ui-script" || argument == "--desktop")
			Engine::showWindow = false;

		if (argument == "--desktop" && i + 1 < argc)
			desktop = argv[i + 1];

		if (argument == "--log-scripts-project")
			Editor::logScriptsProject = true;
	};

	if (!Engine::showWindow)
		ReportCrashesToStderr();

	// Ahead of builds and other busy programs, which otherwise hold up its frames for tens of milliseconds at a time.
	// A hidden run stays behind, so it never holds up the user's. Linux lets only root raise it.
#ifdef _WIN32
	if (Engine::showWindow)
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#endif

	// Nobody hears an automated run, and its sounds would play over the user.
	Audio::useDevice = Engine::showWindow;

	// The new editor starts once this one has freed its MCP port and saved.
	std::filesystem::path relaunch;
	std::string relaunch_arguments;
	bool failed = false;

	{
		// The Engine owns the window, renderer and ImGui context the editor draws into.
		Engine engine;

		// An automated run is often a scratch build, so it leaves the file types alone.
		if (Engine::showWindow)
			ClaimFileTypes();

		glfwSetWindowTitle(Engine::window, "Loom Editor");
		glfwSetWindowSize(Engine::window, 1600, 900);

		if (!desktop.empty() && !ShowOnDesktop(Engine::window, desktop))
			return 1;

		// Opening a project moves the working directory, so command-line paths resolve first.
		const std::filesystem::path launch_folder = std::filesystem::current_path();

		Editor editor;

		bool compile = false;
		bool play = false;
		bool play_web = false;
		bool open_project_dialog = false;
		bool settings = false;
		std::string screenshot;
		std::string screenshot_window;
		std::string preview;
		std::vector<std::string> selection;
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

				// Enough frames for a build to finish and the scene to tick.
				if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0]))
					frames = atoi(argv[++i]);
			}
			else if (argument == "--screenshot-window")
			{
				if (i + 1 >= argc)
				{
					std::cerr << "--screenshot-window needs a window's name" << std::endl;
					return 1;
				};

				screenshot_window = argv[++i];
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

				// Picks the parent the way a dragged-in file does.
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
					std::cerr << "--create-prompt needs script, state, state-machine, dialogue-manager or test" << std::endl;
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

				selection.push_back(argv[++i]);
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
			else if (argument == "--vulkan" || argument == "--opengl" || argument == "--log-scripts-project")
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
					std::cerr << "--open-sprite needs an image or sprite file" << std::endl;
					return 1;
				};

				editor.OpenSprite(argv[++i]);
			}
			else if (argument == "--open-model")
			{
				if (i + 1 >= argc)
				{
					std::cerr << "--open-model needs a prefab or a model" << std::endl;
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
			else if (argument == "--run-tests")
				editor.QueueTestRun(i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0 ? argv[++i] : "");
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

		// The Play button's path, so the scripts compile first.
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

		for (const std::string& name : selection)
			editor.RequestSelection(name);

		if (!screenshot.empty())
			editor.RequestScreenshot(screenshot, frames, true, screenshot_window);

		editor.opensVisualStudio = screenshot.empty() && !exit_after_script;

		// Only one UiScript can listen to ImGui's items.
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
