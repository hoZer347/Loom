#include "Textures.h"

#include "Renderer.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include "stb_image.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unordered_map>


namespace Loom
{
	namespace
	{
		constexpr int RGBA = 4;

		std::unordered_map<std::string, Texture*> shared{ };
	};

	Texture::Texture(const std::string& file_path) :
		file_path(file_path),
		pixels(Decode(file_path, width, height))
	{
		if (Renderer* renderer = Renderer::Get())
			handle = renderer->CreateTexture(width, height, pixels.data());
	};

	std::vector<uint8_t> Texture::Decode(const std::string& file_path, int& width, int& height)
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

		std::vector<uint8_t> pixels(decoded, decoded + (size_t)width * height * RGBA);
		stbi_image_free(decoded);

		return pixels;
	};

	Texture::~Texture()
	{
		if (Renderer* renderer = Renderer::Get(); renderer && handle)
			renderer->DestroyTexture(handle);
	};

	Texture* Texture::Shared(const std::string& file_path)
	{
		const auto found = shared.find(file_path);

		if (found != shared.end())
			return found->second;

		try
		{
			return shared[file_path] = new Texture(file_path);
		}
		catch (const std::exception& e)
		{
			std::cerr << "Texture: " << e.what() << std::endl;
			return nullptr;
		};
	};

	void Texture::ReloadShared(const std::string& file_path)
	{
		for (const auto& [path, texture] : shared)
		{
			std::error_code code;

			if (!std::filesystem::equivalent(path, file_path, code))
				continue;

			try
			{
				int width = 0;
				int height = 0;
				texture->pixels = Decode(file_path, width, height);
				texture->width = width;
				texture->height = height;
			}
			catch (const std::exception& e)
			{
				std::cerr << "Texture: " << e.what() << std::endl;
				continue;
			};

			Renderer* renderer = Renderer::Get();

			if (renderer == nullptr)
				continue;

			if (texture->handle)
				renderer->DestroyTexture(texture->handle);

			texture->handle = renderer->CreateTexture(texture->width, texture->height, texture->pixels.data());
		};
	};
};
