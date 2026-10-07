// Before anything that brings in GLFW, which defines APIENTRY itself
// when windows.h has not been seen.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shlobj.h>

#include "Editor.h"
#include "ProjectTemplate.h"
#include "SceneSerializer.h"

#include "Engine.h"
#include "OpenGL.h"
#include "Scene.h"

#include <cctype>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

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
			<< "  --compile                      build the project's scripts on startup\n"
			<< "  --play                         start with the scene running\n"
			<< "  --play-web                     build the scene for the web and open it\n"
			<< "  --import <file> [x y]          import a model, as if dropped on the window\n"
			<< "  --preview <file>               select a model file, showing it in the Inspector\n"
			<< "  --select <name>                select the first GameObject called that\n"
			<< "  --open-scene-dialog            start with File > Open Scene... open\n"
			<< "  --settings                     start with the Settings window open\n"
			<< "  --open-demo <name>             open a project from the engine's Demos folder\n"
			<< "  --undo, --redo                 step the history once the imports have landed\n"
			<< "  --vulkan, --opengl             run on that graphics API this time\n"
			<< "  --screenshot <file> [frames]   write the editor to a bitmap and exit,\n"
			<< "                                 without showing a window\n"
			<< "  --desktop <id>                 show the window on that virtual desktop,\n"
			<< "                                 without activating it"
			<< std::endl;
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
	// A screenshot run is automated by definition: it renders, writes its
	// bitmap and exits, so it does that without putting a window on screen.
	// Read before the Engine, which is what opens the window.
	//
	// A run on another virtual desktop starts hidden too, and is shown once it
	// is over there.
	std::string desktop;

	Engine::backend = Editor::SavedBackend();

	for (int i = 1; i < argc; i++)
	{
		const std::string argument = argv[i];

		if (argument == "--vulkan")
			Engine::backend = Backend::Vulkan;

		if (argument == "--opengl")
			Engine::backend = Backend::OpenGL;

		if (argument == "--screenshot" || argument == "--desktop")
			Engine::showWindow = false;

		if (argument == "--desktop" && i + 1 < argc)
			desktop = argv[i + 1];
	};

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

	Editor editor;

	bool compile = false;
	bool play = false;
	bool play_web = false;
	bool open_scene_dialog = false;
	bool settings = false;
	std::string screenshot;
	std::string preview;
	std::string select;
	int frames = 240;

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
		else if (argument == "--select")
		{
			if (i + 1 >= argc)
			{
				std::cerr << "--select needs a GameObject name" << std::endl;
				return 1;
			};

			select = argv[++i];
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
		else if (argument == "--open-scene-dialog")
			open_scene_dialog = true;
		else if (argument == "--settings")
			settings = true;
		else if (argument == "--open-demo")
		{
			if (i + 1 >= argc)
			{
				std::cerr << "--open-demo needs a demo name" << std::endl;
				return 1;
			};

			editor.OpenDemo(argv[++i]);
		}
		else if (argument == "--undo" || argument == "--redo")
			editor.QueueHistoryStep(argument == "--undo");
		else if (argument.rfind("--", 0) == 0)
		{
			std::cerr << "Unknown option " << argument << std::endl;
			Usage();
			return 1;
		}
		else editor.OpenProject(argument);
	};

	if (compile)
		editor.CompileScripts();

	// The same path the Play button takes, so a script library that has
	// fallen behind its sources is rebuilt here too.
	if (play)
		editor.TogglePlay();

	if (play_web)
		editor.PlayWeb();

	if (open_scene_dialog)
		editor.ShowOpenSceneDialog();

	if (settings)
		editor.ShowSettings();

	// After the project is open, since opening its scene selects that instead.
	if (!preview.empty())
		editor.SelectModel(preview);

	// Queued, so it runs once the scene's GameObjects, which are queued
	// themselves, are in place.
	if (!select.empty())
		Engine::QueueTask(
			[&editor, select]()
			{
				const std::function<GameObject* (GameObject&)> find =
					[&find, &select](GameObject& gameObject) -> GameObject*
					{
						if (gameObject.GetName() == select)
							return &gameObject;

						for (GameObject* child : gameObject.GetChildren())
							if (GameObject* found = find(*child))
								return found;

						return nullptr;
					};

				for (Scene* scene : Scene::GetScenes())
					if (GameObject* found = find(scene->GetRoot()))
					{
						editor.Select(found);
						return;
					};

				std::cerr << "--select: no GameObject called " << select << std::endl;
			});

	if (!screenshot.empty())
		editor.RequestScreenshot(screenshot, frames, true);

	engine.Start();

	return 0;
};
