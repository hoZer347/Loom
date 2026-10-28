// Loom Web Player
// ---------------
// What the editor's Play Web builds: the engine and a project's scripts behind
// an entry point that loads one scene file and runs it. The page preloads the
// scene and names its path as the first argument, and preloads the project's
// key bindings, when it has any, at input_actions. A page that holds the
// player can edit a shader by calling loom_edit_shader, and hears whether it
// compiled from a "loomshader" event on its window.

#include "Engine.h"
#include "InputActions.h"
#include "Material.h"
#include "SceneSerializer.h"

#include <emscripten.h>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace Loom;

static constexpr const char* input_actions = "/project.loominput";


// Queued, so it compiles between frames on the thread that owns the context.
extern "C" EMSCRIPTEN_KEEPALIVE void loom_edit_shader(const char* path, const char* source)
{
	Engine::QueueTask(
		[path = std::string(path), source = std::string(source)]()
		{
			const std::string error = Material::EditShader(path, source);

			EM_ASM(
				{
					dispatchEvent(new CustomEvent("loomshader", { detail: { path: UTF8ToString($0), error: UTF8ToString($1) } }));
				},
				path.c_str(),
				error.c_str());
		});
};

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::cerr << "Loom Player: no scene to load" << std::endl;
		return EXIT_FAILURE;
	};

	// Before the scene: its components reach for the GL context as they attach.
	Engine engine;

	InputActions::Load(input_actions);

	std::string error;

	if (SceneSerializer::LoadFromFile(argv[1], &error) == nullptr)
	{
		std::cerr << "Loom Player: " << error << std::endl;
		return EXIT_FAILURE;
	};

	engine.Start();

	return EXIT_SUCCESS;
};
