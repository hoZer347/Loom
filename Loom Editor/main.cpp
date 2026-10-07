#include "Editor.h"

#include "Engine.h"
#include "OpenGL.h"
#include "Scene.h"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace Loom;


namespace
{
	void Usage()
	{
		std::cout
			<< "Loom Editor [project folder]\n"
			<< "  --new-project <folder> <name>  create a project and open it\n"
			<< "  --compile                      build the project's scripts on startup\n"
			<< "  --play                         start with the scene running\n"
			<< "  --select <name>                select the GameObject with this name\n"
			<< "  --screenshot <file> [frames]   write the editor to a bitmap and exit,\n"
			<< "                                 without showing a window"
			<< std::endl;
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
	for (int i = 1; i < argc; i++)
		if (std::string(argv[i]) == "--screenshot")
			Engine::showWindow = false;

	// The Engine comes first: it owns the window, the GL context and the ImGui
	// context the editor draws itself into.
	Engine engine;

	glfwSetWindowTitle(Engine::window, "Loom Editor");
	glfwSetWindowSize(Engine::window, 1600, 900);

	Editor editor;

	bool compile = false;
	bool play = false;
	std::string screenshot;
	std::string selection;
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
		else if (argument == "--select")
		{
			if (i + 1 >= argc)
			{
				std::cerr << "--select needs a GameObject name" << std::endl;
				return 1;
			};

			selection = argv[++i];
		}
		else if (argument == "--compile")
			compile = true;
		else if (argument == "--play")
			play = true;
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

	if (!selection.empty())
		editor.RequestSelection(selection);

	if (!screenshot.empty())
		editor.RequestScreenshot(screenshot, frames, true);

	engine.Start();

	return 0;
};
