#pragma once

#include "Loom API.h"

#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <iostream>

struct GLFWwindow;


namespace Loom
{
	typedef std::function<void()> Task;
	struct Scene;

	/**
	* Loom::Engine
	* - Manages the runtime of the application
	* - Updates all GameObjects
	* - Manages the backend OpenGL and ImGui loops
	* - Should decouple OpenGL and ImGui, but currently does not
	*/
	struct LOOM_API Engine final
	{
		Engine();
		~Engine();

		void Start();

		// Static so anything holding no Engine reference (a tool, a UI callback)
		// can still ask the loop to end; it only ever touches Engine statics.
		static void Stop();

		static void SetUpdateFunction(const Task& task);

		// Replaces the built-in per-scene ImGui windows with a custom one. Called
		// from inside the ImGui frame, so it is safe to issue ImGui (and raw GL)
		// calls from it. Pass an empty Task to fall back to the default windows.
		static void SetGuiFunction(const Task& task);

		static void QueueTask(const Task& task);

		// Runs everything QueueTask has collected so far, on the calling
		// thread. renderFrame calls this once a frame; it is public so a host
		// that drives the engine itself (a test, a tool) can flush deferred
		// work without a window or a GL context.
		static void DoTasks() noexcept;

		static inline bool doGUI = true;

		// Whether the window the engine opens is shown. Set before the Engine is
		// constructed, because that is what opens it: an automated run has nobody
		// to show it to, and a window appearing takes the screen and the focus
		// off whoever is at the machine.
		static inline bool showWindow = true;

		// Whether renderFrame ticks / draws the registered scenes itself. An
		// editor turns rendering off so it can draw the scene into its own
		// framebuffer instead, and drives updating with its play controls.
		static inline bool updateScenes = true;
		static inline bool renderScenes = true;

		static inline unsigned int shaderProgram = 0;
		static inline unsigned int VAO = 0;
		static inline unsigned int VBO = 0;
		static inline unsigned int EBO = 0;

		static const size_t GetUniqueID();

		// The colour the frame is cleared to, as RGBA. Settable so a scene can
		// match whatever it is embedded in; renderFrame reads it every frame.
		static inline float clearColor[4] = { 0.2f, 0.3f, 0.3f, 1.0f };

		static inline GLFWwindow* window = nullptr;

		static void renderFrame();

		static inline bool isRunning;

	private:
		static void InitImGui();
		static void RenderImGui();

		static inline const char* projectDirectory;

		static inline Task onUpdate = []() { };
		static inline Task onGui = nullptr;

		static inline std::recursive_mutex mutex;
		static inline std::queue<Task> taskQueue;
	};
};
