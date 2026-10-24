// Loom Player
// -----------
// What the editor's Build Settings tab builds: the engine and a project's
// scripts in one program, which runs from its own folder beside a copy of the
// project's files, starting on the scene its <Game>.loomgame names. Given a
// scene, it runs that from the folder it was started in instead. Debug and
// Release show the stage timing over the game, and a final build has none.
//
//   <Game>.exe [scene] [--screenshot <file> [frames]] [--collapsed]
//
// --screenshot runs it hidden, writes the frame once that many have been
// drawn, and ends. --collapsed starts the stage timing collapsed. Started from
// Explorer it has no console, so what stops it starting is shown in a box too.

#include "Audio.h"
#include "Engine.h"
#include "InputActions.h"
#include "SceneSerializer.h"
#include "StageTiming.h"

#include "imgui.h"

#include <GLFW/glfw3.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

using namespace Loom;

// Enough for a scene to have loaded and been ticked before it is captured.
static constexpr int default_frames = 240;

// Beside the program, naming the scene a build starts on.
static constexpr const char* game_extension = ".loomgame";


static std::filesystem::path Program()
{
	std::wstring path(MAX_PATH, L'\0');

	while (GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size()) == path.size())
		path.resize(path.size() * 2);

	return std::filesystem::path(path.c_str());
};

// The scene a build starts on, which build-game.ps1 writes beside the program.
static std::string StartScene(const std::filesystem::path& program)
{
	constexpr std::string_view scene_key = "scene=";

	std::ifstream in(std::filesystem::path(program).replace_extension(game_extension));
	std::string line;

	while (std::getline(in, line))
	{
		while (!line.empty() && line.back() == '\r')
			line.pop_back();

		if (line.starts_with(scene_key))
			return line.substr(scene_key.size());
	};

	return "";
};

// The project's key bindings, copied in with the rest of its files.
static std::string InputActionsFile()
{
	std::error_code code;

	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(std::filesystem::current_path(), code))
		if (entry.path().extension() == InputActions::extension)
			return entry.path().string();

	return "";
};

static int Fail(const std::filesystem::path& program, const std::string& message, bool shown)
{
	std::cerr << "Loom Player: " << message << std::endl;

	if (shown)
		MessageBoxW(nullptr, std::filesystem::path(message).wstring().c_str(), program.stem().wstring().c_str(), MB_OK | MB_ICONERROR);

	return EXIT_FAILURE;
};

int main(int argc, char** argv)
{
	const std::filesystem::path program = Program();
	const bool given = argc > 1 && !std::string_view(argv[1]).starts_with("--");
	std::string scene = given ? argv[1] : "";

	std::string screenshot;
	int frames = default_frames;
	bool collapsed = false;

	for (int i = given ? 2 : 1; i < argc; i++)
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
			return Fail(program, argument + " is not an option", screenshot.empty());
	};

	if (!given)
	{
		std::error_code code;
		std::filesystem::current_path(program.parent_path(), code);

		scene = StartScene(program);
	};

	if (scene.empty())
		return Fail(program, "No scene to start on: " + std::filesystem::path(program).replace_extension(game_extension).string() + " names none.", screenshot.empty());

	// Nobody is watching an automated run, nor listening to it.
	Engine::showWindow = screenshot.empty();
	Audio::useDevice = Engine::showWindow;

	// Before the scene: its components reach for the GL context as they attach.
	Engine engine;

	glfwSetWindowTitle(Engine::window, program.stem().string().c_str());

	// What the player shows is the game's, not left in the project between runs.
	ImGui::GetIO().IniFilename = nullptr;

	InputActions::Load(InputActionsFile());

	std::string error;

	if (SceneSerializer::LoadFromFile(scene, &error) == nullptr)
		return Fail(program, error, Engine::showWindow);

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
