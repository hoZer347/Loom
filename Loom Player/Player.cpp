// Loom Player
// -----------
// What the editor's Build menu builds: the engine and a project's scripts in
// one program, which loads the scene file it is given and runs it from the
// project folder it is started in. Debug and Release show the stage timing
// over the game, and a final build has none.
//
//   <Game>.exe <scene> [--screenshot <file> [frames]] [--collapsed]
//
// --screenshot runs it hidden, writes the frame once that many have been
// drawn, and ends. --collapsed starts the stage timing collapsed.

#include "Audio.h"
#include "Engine.h"
#include "InputActions.h"
#include "SceneSerializer.h"
#include "StageTiming.h"

#include "imgui.h"

#include <GLFW/glfw3.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace Loom;

// Enough for a scene to have loaded and been ticked before it is captured.
static constexpr int default_frames = 240;


// The project's key bindings, beside its project file in the folder it runs from.
static std::string InputActionsFile()
{
	constexpr const char* project_extension = ".loomproject";

	std::error_code code;

	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(std::filesystem::current_path(), code))
		if (entry.path().extension() == project_extension)
			return std::filesystem::path(entry.path()).replace_extension(InputActions::extension).string();

	return "";
};

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::cerr << "Loom Player: no scene to load" << std::endl;
		return EXIT_FAILURE;
	};

	std::string screenshot;
	int frames = default_frames;
	bool collapsed = false;

	for (int i = 2; i < argc; i++)
	{
		const std::string argument = argv[i];

		if (argument == "--screenshot" && i + 1 < argc)
		{
			screenshot = argv[++i];

			if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0]))
				frames = atoi(argv[++i]);
		}
		else if (argument == "--collapsed")
			collapsed = true;
		else
		{
			std::cerr << "Loom Player: " << argument << " is not an option" << std::endl;
			return EXIT_FAILURE;
		};
	};

	// Nobody is watching an automated run, nor listening to it.
	Engine::showWindow = screenshot.empty();
	Audio::useDevice = Engine::showWindow;

	// Before the scene: its components reach for the GL context as they attach.
	Engine engine;

	glfwSetWindowTitle(Engine::window, std::filesystem::path(argv[0]).stem().string().c_str());

	// What the player shows is the game's, not left in the project between runs.
	ImGui::GetIO().IniFilename = nullptr;

	InputActions::Load(InputActionsFile());

	std::string error;

	if (SceneSerializer::LoadFromFile(argv[1], &error) == nullptr)
	{
		std::cerr << "Loom Player: " << error << std::endl;
		return EXIT_FAILURE;
	};

#ifdef LOOM_FINAL
	Engine::doGUI = false;
#else
	Engine::SetGuiFunction(
		[collapsed]()
		{
			ImGui::SetNextWindowCollapsed(collapsed, ImGuiCond_Once);
			StageTiming::Overlay();
		});
#endif

	if (!screenshot.empty())
		Engine::SetUpdateFunction(
			[screenshot, frames, frame = 0]() mutable
			{
				if (++frame < frames)
					return;

				Engine::CaptureFrame(screenshot);
				Engine::Stop();
			});

	engine.Start();

	return EXIT_SUCCESS;
};
