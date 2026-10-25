#pragma once

// The Engine's own renderer, for the cases that need to see what is drawn.
//
// RenderFixture opens it on a hidden window, as an automated editor run does,
// so a case draws through the real GL, reads the pixels back and saves them
// as a screenshot. Desktop only: the web suite runs under node, which has no
// GL to draw with.

#ifndef __EMSCRIPTEN__

#include "Test Support.h"

#include "Engine.h"
#include "Renderer.h"

#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include "imgui.h"
#include "stb_image_write.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>


namespace LoomTests
{
	// Where Image::Save writes: Screenshots beside the suite's executable,
	// which main empties as a run starts, so it holds only this run's.
	inline std::filesystem::path& ScreenshotFolder()
	{
		static std::filesystem::path folder;
		return folder;
	};

	// A frame read back from the renderer.
	struct Image
	{
		static constexpr int BYTES_PER_PIXEL = 3;

		int width = 0;
		int height = 0;

		// RGB, top row first.
		std::vector<uint8_t> rgb{ };

		// From the top left, as an image viewer counts.
		glm::u8vec3 At(int x, int y) const
		{
			const uint8_t* pixel = rgb.data() + ((size_t)y * width + x) * BYTES_PER_PIXEL;
			return glm::u8vec3(pixel[0], pixel[1], pixel[2]);
		};

		// As name.png in ScreenshotFolder, answering its path.
		std::filesystem::path Save(const std::string& name) const
		{
			std::filesystem::create_directories(ScreenshotFolder());

			const std::filesystem::path path = ScreenshotFolder() / (name + ".png");
			std::ofstream file(path, std::ios::binary);

			// The engine builds stb's writer without its file functions.
			const auto write =
				[](void* context, void* data, int size)
				{
					static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
				};

			if (!stbi_write_png_to_func(write, &file, width, height, BYTES_PER_PIXEL, rgb.data(), width * BYTES_PER_PIXEL) || !file)
				throw std::runtime_error("Could not write " + path.string());

			return path;
		};
	};

	// The Engine, on a hidden window size pixels across, for as long as it
	// lives. Its GUI is off, so a frame holds only what the case draws.
	struct RenderFixture : EngineFixture
	{
		static constexpr glm::ivec2 DEFAULT_SIZE{ 160, 90 };

		explicit RenderFixture(glm::ivec2 size = DEFAULT_SIZE) :
			m_showWindow(Loom::Engine::showWindow),
			m_vsync(Loom::Engine::vsync),
			m_doGUI(Loom::Engine::doGUI),
			m_backend(Loom::Engine::backend)
		{
			Loom::Engine::showWindow = false;
			Loom::Engine::vsync = false;
			Loom::Engine::doGUI = false;
			Loom::Engine::backend = Loom::Backend::OpenGL;

			m_engine = std::make_unique<Loom::Engine>();

			if (!Loom::Engine::window)
				throw std::runtime_error("The Engine could not open a hidden window to render into");

			ImGui::GetIO().IniFilename = nullptr;
			glfwSetWindowSize(Loom::Engine::window, size.x, size.y);
			glfwPollEvents();
		};

		~RenderFixture()
		{
			// What the case's objects queued as they went, while there is still
			// a renderer for it to release.
			Pump();

			m_engine.reset();

			Loom::Engine::showWindow = m_showWindow;
			Loom::Engine::vsync = m_vsync;
			Loom::Engine::doGUI = m_doGUI;
			Loom::Engine::backend = m_backend;
		};

		RenderFixture(const RenderFixture&) = delete;
		RenderFixture& operator=(const RenderFixture&) = delete;

		// One whole frame of the Engine, the scenes drawn as a game draws them.
		Image Frame()
		{
			return Read([]() { Loom::Engine::renderFrame(); });
		};

		// The window cleared to Engine::clearColor, then whatever draw draws.
		Image Draw(const std::function<void()>& draw)
		{
			Loom::Renderer& renderer = *Loom::Renderer::Get();

			return Read(
				[&]()
				{
					renderer.BeginFrame();
					renderer.Clear(Loom::Engine::clearColor, true);
					draw();
					renderer.EndFrame();
				});
		};

	private:
		Image Read(const std::function<void()>& frame)
		{
			Image image;

			Loom::Renderer::Get()->Capture(
				[&image](int width, int height, const std::vector<uint8_t>& rgb)
				{
					image.width = width;
					image.height = height;
					image.rgb.resize(rgb.size());

					// The renderer hands rows back bottom first.
					const size_t row = (size_t)width * Image::BYTES_PER_PIXEL;

					for (int y = 0; y < height; y++)
						std::copy_n(rgb.data() + (size_t)(height - 1 - y) * row, row, image.rgb.data() + (size_t)y * row);
				});

			frame();

			return image;
		};

		bool m_showWindow;
		bool m_vsync;
		bool m_doGUI;
		Loom::Backend m_backend;

		std::unique_ptr<Loom::Engine> m_engine;
	};
};

#endif
