#include "Engine.h"

#include "imgui.h"

#include "OpenGL.h"
#include "Renderer.h"
#include "Scene.h"
#include "Input.h"
#include "Utilities/Clock.h"

#include <iostream>
#include <atomic>
#include <fstream>
#include <vector>


namespace Loom
{
	// settings
	const unsigned int SCR_WIDTH = 800;
	const unsigned int SCR_HEIGHT = 600;

	ImGuiIO* io;

	namespace
	{
		// GL hands pixels back red first; a bitmap stores them blue first.
		enum Channel { RED, GREEN, BLUE, BYTES_PER_PIXEL };
		enum BitmapChannel { BITMAP_BLUE, BITMAP_GREEN, BITMAP_RED };

		std::unique_ptr<Renderer> renderer;

		// Writes a 24 bit bottom-up bitmap, which is the row order GL hands back
		// and the one format worth writing without an image library.
		bool WriteBitmap(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb)
		{
			constexpr uint32_t FILE_HEADER = 14;
			constexpr uint32_t INFO_HEADER = 40;
			constexpr uint16_t PLANES = 1;
			constexpr uint16_t BITS_PER_PIXEL = 24;
			constexpr uint32_t PIXELS_PER_METRE = 2835;	// 72 dpi
			constexpr uint32_t ROW_ALIGNMENT = 4;
			constexpr char SIGNATURE[] = { 'B', 'M' };

			std::ofstream out(path, std::ios::binary);

			if (!out)
				return false;

			const uint32_t packed = (uint32_t)width * BYTES_PER_PIXEL;
			const uint32_t row = (packed + ROW_ALIGNMENT - 1) & ~(ROW_ALIGNMENT - 1);
			const uint32_t image = row * (uint32_t)height;
			const uint32_t offset = FILE_HEADER + INFO_HEADER;

			const auto put16 = [&out](uint16_t v) { out.write((const char*)&v, sizeof(v)); };
			const auto put32 = [&out](uint32_t v) { out.write((const char*)&v, sizeof(v)); };

			out.write(SIGNATURE, sizeof(SIGNATURE));
			put32(offset + image); put32(0); put32(offset);
			put32(INFO_HEADER); put32((uint32_t)width); put32((uint32_t)height);
			put16(PLANES); put16(BITS_PER_PIXEL); put32(0); put32(image);
			put32(PIXELS_PER_METRE); put32(PIXELS_PER_METRE); put32(0); put32(0);

			// Read as RGB and swizzled here, because GLES has no BGR.
			std::vector<char> line(row, 0);

			for (int y = 0; y < height; y++)
			{
				const uint8_t* source = rgb.data() + (size_t)y * packed;

				for (int x = 0; x < width; x++)
				{
					char* to = line.data() + x * BYTES_PER_PIXEL;
					const uint8_t* from = source + x * BYTES_PER_PIXEL;

					to[BITMAP_BLUE] = (char)from[BLUE];
					to[BITMAP_GREEN] = (char)from[GREEN];
					to[BITMAP_RED] = (char)from[RED];
				};

				out.write(line.data(), (std::streamsize)row);
			};

			return (bool)out;
		};
	};

	void Engine::InitImGui()
	{
		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		io = &ImGui::GetIO(); (void)io;

		io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
		io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking

#ifndef __EMSCRIPTEN__
		// Multi-viewport (native only). Not while the window is hidden: a panel
		// dragged out in an earlier session is remembered in the layout, and a
		// viewport is a real window that ImGui shows whatever this one does.
		if (showWindow)
		{
			io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport / Platform Windows
			io->ConfigViewportsNoAutoMerge = true;
		};
#endif

		// Setup Dear ImGui style
		ImGui::StyleColorsLight();

		// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
		ImGuiStyle& style = ImGui::GetStyle();
		if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		};

		renderer->InitImGui(Engine::window);
	};

	void Engine::RenderImGui()
	{
		// Start the Dear ImGui frame
		renderer->NewImGuiFrame();
		ImGui::NewFrame();

		// GUI
		if (onGui)
			onGui();
		else if (Engine::doGUI)
			for (auto& scene : Scene::allScenes)
			{
				if (ImGui::Begin(scene->NameAndID().c_str()))
					scene->root.Gui();
				ImGui::End();
			};

		// Rendering
		ImGui::Render();
		renderer->RenderImGui(ImGui::GetDrawData());

		if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			renderer->RenderImGuiWindows();
	};

	void Engine::Start()
	{
#if __EMSCRIPTEN__
		emscripten_set_main_loop(renderFrame, 0, true);
#else
		while (!glfwWindowShouldClose(window))
			renderFrame();
#endif

		isRunning = false;
	};

	void Engine::Stop()
	{
		isRunning = false;
		glfwSetWindowShouldClose(window, true);
	}

	void Engine::renderFrame()
	{
		glfwPollEvents();

		Input::Tick();

		// The frame clock, the edge-detected dialogue input and the StaticStateMachine.
		// First thing in the frame, so everything ticked below reads the same delta and
		// the same key presses.
		Utilities::Tick();

		renderer->BeginFrame();
		renderer->Clear(clearColor, true);

		DoTasks();

		onUpdate();

		//Shader::SetUniforms();

#ifndef __EMSCRIPTEN__
		RenderImGui();
#endif

		if (updateScenes)
			for (auto& scene : Scene::allScenes)
				scene->Update();

		if (renderScenes)
			for (auto& scene : Scene::allScenes)
				scene->Render();

		renderer->EndFrame();
	};

	void Engine::CaptureFrame(const std::string& path)
	{
		renderer->Capture(
			[path](int width, int height, const std::vector<uint8_t>& rgb)
			{
				if (width <= 0 || height <= 0)
					std::cerr << "Nothing to capture: the window has no size" << std::endl;
				else if (!WriteBitmap(path, width, height, rgb))
					std::cerr << "Could not write " << path << std::endl;
				else std::cout << "Wrote " << path << " (" << width << 'x' << height << ')' << std::endl;
			});
	};

	void Engine::SetUpdateFunction(const Task& task)
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		onUpdate = task;
	};

	void Engine::SetGuiFunction(const Task& task)
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		onGui = task;
	};

	void Engine::QueueTask(const Task& task)
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		taskQueue.push(task);
	};

	void Engine::DoTasks() noexcept
	{
		// Each task is popped under the lock but run outside it, so a task is
		// free to queue more work (Attach doing so from OnAttach is normal) and
		// the follow-up still runs in this same drain.
		while (true)
		{
			Task task;

			{
				std::lock_guard<std::recursive_mutex> lock(mutex);

				if (taskQueue.empty())
					return;

				task = taskQueue.front();
				taskQueue.pop();
			};

			// DoTasks is noexcept because it runs from the frame loop, where an
			// escaping exception would take the whole application down.
			try { task(); }
			catch (const std::exception& e)
			{
				std::cerr << "Queued task threw: " << e.what() << std::endl;
			}
			catch (...)
			{
				std::cerr << "Queued task threw an unknown exception" << std::endl;
			};
		};
	};

	const size_t Engine::GetUniqueID()
	{
		static std::atomic<size_t> id = 0;
		return id++;
	};

	Engine::Engine()
	{
		if (!glfwInit())
			std::cerr << "GLFW failed to init" << std::endl;

		window = OpenWindow(backend);

		// Vulkan needs a driver and features an OpenGL machine may not have, and
		// a window made for one API cannot be handed to the other.
		if (!window && backend == Backend::Vulkan)
		{
			std::cerr << "Falling back to OpenGL" << std::endl;
			backend = Backend::OpenGL;
			window = OpenWindow(backend);
		};

		if (!window)
		{
			std::cerr << "Failed to create a window" << std::endl;
			return;
		};

		Renderer::current = renderer.get();

		Input::Init();

		InitImGui();
	};

	GLFWwindow* Engine::OpenWindow(Backend api)
	{
		glfwDefaultWindowHints();
		renderer = Renderer::Create(api);

		if (!showWindow)
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

		GLFWwindow* opened = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "", NULL, NULL);

		if (opened && renderer->Attach(opened))
			return opened;

		renderer.reset();

		if (opened)
			glfwDestroyWindow(opened);

		return nullptr;
	};

	Engine::~Engine()
	{
		Stop();

		// After whatever the application built on the Engine is gone, since
		// that is what holds the textures and programs being released here.
		if (renderer)
		{
			renderer->ShutdownImGui();
			ImGui::DestroyContext();

			Renderer::current = nullptr;
			renderer.reset();
		};

		glfwTerminate();
		window = nullptr;
	};
};
