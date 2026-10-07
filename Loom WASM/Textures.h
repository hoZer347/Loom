#pragma once

#include "Loom API.h"

#include <cstdint>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::Texture
	* - An image file decoded to eight bit RGBA, bottom row first, and the
	*   renderer texture made from it
	* - The pixels stay on the CPU side too, for whatever wants to read them
	* - With no renderer (a test with no window) there is no handle, but the
	*   pixels are still decoded
	*/
	struct LOOM_API Texture final
	{
		// Throws std::runtime_error when the file cannot be read or decoded.
		explicit Texture(const std::string& file_path);
		~Texture();

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		// One Texture per path, shared by everything that names it, and kept
		// until the process ends. Null when the file will not load; a failure is
		// not remembered, so fixing the file is enough to have it load next time.
		static Texture* Shared(const std::string& file_path);

		const std::string file_path;

		int width = 0;
		int height = 0;
		std::vector<uint8_t> pixels;

		uint32_t handle = 0;
	};
};
