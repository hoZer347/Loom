#include "Textures.h"

#include "Renderer.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include "stb_image.h"

#include <iostream>
#include <stdexcept>
#include <unordered_map>


namespace Loom
{
	namespace
	{
		constexpr int RGBA = 4;
	};

	Texture::Texture(const std::string& file_path) :
		file_path(file_path)
	{
		// Images are stored top row first; textures are uploaded bottom row first.
		stbi_set_flip_vertically_on_load(true);

		int channels = 0;
		stbi_uc* decoded = stbi_load(
			file_path.c_str(),
			&width,
			&height,
			&channels,
			RGBA);

		if (decoded == nullptr)
			throw std::runtime_error("Could not load texture " + file_path + ": " + stbi_failure_reason());

		pixels.assign(decoded, decoded + (size_t)width * height * RGBA);
		stbi_image_free(decoded);

		if (Renderer* renderer = Renderer::Get())
			handle = renderer->CreateTexture(width, height, pixels.data());
	};

	Texture::~Texture()
	{
		if (Renderer* renderer = Renderer::Get(); renderer && handle)
			renderer->DestroyTexture(handle);
	};

	Texture* Texture::Shared(const std::string& file_path)
	{
		static std::unordered_map<std::string, Texture*> textures{ };

		const auto found = textures.find(file_path);

		if (found != textures.end())
			return found->second;

		try
		{
			return textures[file_path] = new Texture(file_path);
		}
		catch (const std::exception& e)
		{
			std::cerr << "Texture: " << e.what() << std::endl;
			return nullptr;
		};
	};
};
