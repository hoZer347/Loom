#include "SpriteAsset.h"

#include "McpProtocol.h"
#include "ProjectAssets.h"
#include "Textures.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>


namespace Loom
{
	namespace
	{
		#pragma region Image

		constexpr int channels = 4;
		constexpr int alpha = 3;
		constexpr float full = (float)UINT8_MAX;

		constexpr int jpeg_quality = 95;

		std::string LowerExtension(const std::string& path)
		{
			std::string extension = std::filesystem::path(path).extension().string();

			std::transform(
				extension.begin(),
				extension.end(),
				extension.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });

			return extension;
		};

		// Texture's rows run bottom up, the way the renderer uploads them.
		void FlipRows(SpriteAsset::Image& image)
		{
			const size_t row = (size_t)image.width * channels;

			for (int y = 0; y < image.height / 2; y++)
				std::swap_ranges(
					image.pixels.begin() + (ptrdiff_t)(y * row),
					image.pixels.begin() + (ptrdiff_t)((y + 1) * row),
					image.pixels.begin() + (ptrdiff_t)((image.height - 1 - y) * row));
		};

		#pragma endregion

		#pragma region Files

		constexpr char separator = '=';
		constexpr std::string_view image_key = "image";
		constexpr std::string_view palette_key = "palette";
		constexpr std::string_view size_key = "size";
		constexpr std::string_view frame_key = "frame";
		constexpr std::string_view layer_key = "layer";
		constexpr std::string_view hidden_key = "hidden";
		constexpr std::string_view outlined_key = "outlined";
		constexpr std::string_view cel_key = "cel";
		constexpr char size_between = 'x';

		bool ParseInt(std::string_view text, int& value)
		{
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);

			return error == std::errc() && end == text.data() + text.size();
		};

		// Written relative to the folder its file is in, so the pair can move
		// together.
		std::string Relative(const std::string& path, const std::filesystem::path& folder)
		{
			const std::filesystem::path relative = std::filesystem::path(path).lexically_relative(folder);

			return relative.empty()
				? std::filesystem::path(path).generic_string()
				: relative.generic_string();
		};

		std::string Resolve(std::string_view value, const std::filesystem::path& folder)
		{
			return (folder / std::filesystem::path(value)).lexically_normal().string();
		};

		std::pair<std::string_view, std::string_view> Split(std::string_view line)
		{
			const size_t at = line.find(separator);

			return at == std::string_view::npos
				? std::pair(line, std::string_view{ })
				: std::pair(line.substr(0, at), line.substr(at + 1));
		};

		// The image of a sprite file with no image= line.
		std::string BesideAsPng(const std::string& path)
		{
			return std::filesystem::path(path).replace_extension(ProjectAssets::textureExtension).string();
		};

		bool WriteFile(const std::string& path, const std::string& text, const char* what)
		{
			std::ofstream file(path, std::ios::binary);

			if (!file.write(text.data(), (std::streamsize)text.size()))
			{
				std::cerr << what << ": could not save " << path << std::endl;
				return false;
			};

			return true;
		};

		#pragma endregion

		#pragma region Palette

		constexpr char colour_mark = '#';
		constexpr char category_open = '[';
		constexpr char category_close = ']';
		constexpr size_t rgb = 3;
		constexpr int hex = 16;
		constexpr size_t hex_digits = 2;

		#pragma endregion
	};

	#pragma region Image

	SpriteAsset::Image SpriteAsset::Image::Blank(int width, int height)
	{
		return { width, height, std::vector<uint8_t>((size_t)width * height * channels) };
	};

	bool SpriteAsset::Image::Contains(int x, int y) const
	{
		return x >= 0 && y >= 0 && x < width && y < height;
	};

	SpriteAsset::Colour SpriteAsset::Image::Get(int x, int y) const
	{
		const uint8_t* pixel = &pixels[((size_t)y * width + x) * channels];

		return { pixel[0], pixel[1], pixel[2], pixel[3] };
	};

	void SpriteAsset::Image::Set(int x, int y, const Colour& colour)
	{
		if (Contains(x, y))
			std::copy(colour.begin(), colour.end(), &pixels[((size_t)y * width + x) * channels]);
	};

	void SpriteAsset::Image::Line(int x0, int y0, int x1, int y1, const Colour& colour)
	{
		// Bresenham's, stepping along whichever axis the line covers more of.
		const int dx = std::abs(x1 - x0);
		const int dy = -std::abs(y1 - y0);
		const int sx = x0 < x1 ? 1 : -1;
		const int sy = y0 < y1 ? 1 : -1;

		int error = dx + dy;

		while (true)
		{
			Set(x0, y0, colour);

			if (x0 == x1 && y0 == y1)
				return;

			const int doubled = error + error;

			if (doubled >= dy)
			{
				error += dy;
				x0 += sx;
			};

			if (doubled <= dx)
			{
				error += dx;
				y0 += sy;
			};
		};
	};

	void SpriteAsset::Image::Fill(int x, int y, const Colour& colour)
	{
		if (!Contains(x, y))
			return;

		const Colour target = Get(x, y);

		if (target == colour)
			return;

		std::vector<std::pair<int, int>> pending = { { x, y } };

		while (!pending.empty())
		{
			const auto [px, py] = pending.back();
			pending.pop_back();

			if (!Contains(px, py) || Get(px, py) != target)
				continue;

			Set(px, py, colour);

			pending.insert(pending.end(), { { px + 1, py }, { px - 1, py }, { px, py + 1 }, { px, py - 1 } });
		};
	};

	bool SpriteAsset::Image::IsClear() const
	{
		for (size_t i = alpha; i < pixels.size(); i += channels)
			if (pixels[i] != 0)
				return false;

		return true;
	};

	void SpriteAsset::Image::Over(const Image& above)
	{
		const size_t count = (std::min)(pixels.size(), above.pixels.size());

		for (size_t i = 0; i < count; i += channels)
		{
			const uint8_t* top = &above.pixels[i];
			uint8_t* bottom = &pixels[i];

			if (top[alpha] == 0)
				continue;

			// Exact where nothing shows through, which is most pixels.
			if (top[alpha] == UINT8_MAX || bottom[alpha] == 0)
			{
				std::copy(top, top + channels, bottom);
				continue;
			};

			const float a = top[alpha] / full;
			const float b = bottom[alpha] / full * (1.0f - a);
			const float out = a + b;

			for (int c = 0; c < alpha; c++)
				bottom[c] = (uint8_t)std::lround((top[c] * a + bottom[c] * b) / out);

			bottom[alpha] = (uint8_t)std::lround(out * full);
		};
	};

	SpriteAsset::Image SpriteAsset::Image::Scaled(int to_width, int to_height) const
	{
		Image scaled = Blank(to_width, to_height);

		for (int y = 0; y < to_height; y++)
			for (int x = 0; x < to_width; x++)
				scaled.Set(x, y, Get((int)((int64_t)x * width / to_width), (int)((int64_t)y * height / to_height)));

		return scaled;
	};

	SpriteAsset::Image SpriteAsset::Image::Placed(int to_width, int to_height, int left, int top) const
	{
		Image placed = Blank(to_width, to_height);

		for (int y = 0; y < height; y++)
			for (int x = 0; x < width; x++)
				placed.Set(left + x, top + y, Get(x, y));

		return placed;
	};

	bool SpriteAsset::LoadImage(const std::string& path, Image& image)
	{
		try
		{
			image.pixels = Texture::Decode(path, image.width, image.height);
		}
		catch (const std::exception& e)
		{
			std::cerr << "Sprites: " << e.what() << std::endl;
			return false;
		};

		FlipRows(image);

		return true;
	};

	std::vector<char> SpriteAsset::Encode(const std::string& extension, const Image& image)
	{
		std::vector<char> bytes;

		const auto append = [](void* context, void* data, int size)
		{
			std::vector<char>& out = *static_cast<std::vector<char>*>(context);
			out.insert(out.end(), (const char*)data, (const char*)data + size);
		};

		const int width = image.width;
		const int height = image.height;
		const uint8_t* pixels = image.pixels.data();

		int written = 0;

		if (extension == ".png")
			written = stbi_write_png_to_func(append, &bytes, width, height, channels, pixels, width * channels);
		else if (extension == ".bmp")
			written = stbi_write_bmp_to_func(append, &bytes, width, height, channels, pixels);
		else if (extension == ".tga")
			written = stbi_write_tga_to_func(append, &bytes, width, height, channels, pixels);
		else if (extension == ".jpg" || extension == ".jpeg")
			written = stbi_write_jpg_to_func(append, &bytes, width, height, channels, pixels, jpeg_quality);

		if (!written)
			bytes.clear();

		return bytes;
	};

	bool SpriteAsset::SaveImage(const std::string& path, const Image& image)
	{
		const std::vector<char> bytes = Encode(LowerExtension(path), image);

		// Checked before the file is opened, which empties it.
		if (bytes.empty())
		{
			std::cerr << "Sprites: could not encode " << path << std::endl;
			return false;
		};

		return WriteFile(path, std::string(bytes.begin(), bytes.end()), "Sprites");
	};

	SpriteAsset::Image SpriteAsset::AsSaved(const std::string& path, const Image& image)
	{
		Image saved;

		try
		{
			saved.pixels = Texture::Decode(Encode(LowerExtension(path), image), saved.width, saved.height);
		}
		catch (const std::exception&)
		{
			return image;
		};

		FlipRows(saved);

		return saved;
	};

	#pragma endregion

	#pragma region Files

	std::string SpriteAsset::For(const std::string& image_path)
	{
		return std::filesystem::path(image_path).replace_extension(extension).string();
	};

	std::string SpriteAsset::ImageOf(const std::string& path)
	{
		std::ifstream file(path);

		if (!file)
			return "";

		std::string line;

		while (std::getline(file, line))
			if (const auto [key, value] = Split(line); key == image_key)
				return Resolve(value, std::filesystem::path(path).parent_path());

		return BesideAsPng(path);
	};

	std::string SpriteAsset::Companion(const std::string& path)
	{
		const std::filesystem::path file = std::filesystem::path(path).lexically_normal();
		const bool sprite = file.extension() == extension;
		const std::filesystem::path other = sprite ? ImageOf(file.string()) : For(file.string());

		std::error_code code;

		if (other.empty() || other.parent_path() != file.parent_path() || !std::filesystem::is_regular_file(other, code))
			return "";

		// A sprite beside an image is its own only when it names it.
		return sprite || std::filesystem::path(ImageOf(other.string())) == file
			? other.string()
			: "";
	};

	bool SpriteAsset::Load(const std::string& path, SpriteAsset& sprite, Links& links)
	{
		std::ifstream file(path, std::ios::binary);

		if (!file)
		{
			std::cerr << "Sprites: could not open " << path << std::endl;
			return false;
		};

		const std::filesystem::path folder = std::filesystem::path(path).parent_path();

		SpriteAsset loaded;
		Links found;
		std::string line;
		bool overfull = false;

		while (std::getline(file, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			const auto [key, value] = Split(line);

			if (key == image_key)
				found.image = Resolve(value, folder);
			else if (key == palette_key && !value.empty())
				found.palette = Resolve(value, folder);
			else if (key == size_key)
			{
				const size_t between = value.find(size_between);

				if (between == std::string_view::npos ||
					!ParseInt(value.substr(0, between), loaded.width) ||
					!ParseInt(value.substr(between + 1), loaded.height) ||
					loaded.width <= 0 || loaded.width > maxSide ||
					loaded.height <= 0 || loaded.height > maxSide)
					loaded.width = loaded.height = 0;
			}
			else if (key == frame_key)
			{
				int duration = defaultDuration;
				ParseInt(value, duration);
				loaded.durations.push_back(std::clamp(duration, minDuration, maxDuration));
			}
			else if (key == layer_key)
				loaded.layers.push_back(Layer{ std::string(value) });
			else if (key == hidden_key && !loaded.layers.empty())
				loaded.layers.back().visible = false;
			else if (key == outlined_key && !loaded.layers.empty())
				loaded.layers.back().outlined = true;
			else if (key == cel_key && !loaded.layers.empty())
			{
				std::vector<Image>& cels = loaded.layers.back().cels;

				// A cel is made before the sprite is checked, so one past its
				// frames is refused before it is.
				if (cels.size() >= std::min<size_t>(loaded.durations.size(), maxFrames))
				{
					overfull = true;
					break;
				};

				try
				{
					Image cel = Image::Blank(loaded.width, loaded.height);

					if (!value.empty())
					{
						cel.pixels = Texture::Decode(McpProtocol::FromBase64(value), cel.width, cel.height);
						FlipRows(cel);
					};

					cels.push_back(std::move(cel));
				}
				catch (const std::exception& e)
				{
					std::cerr << "Sprites: " << path << ": " << e.what() << std::endl;
					return false;
				};
			};
		};

		bool whole =
			!overfull &&
			loaded.width > 0 &&
			loaded.height > 0 &&
			!loaded.durations.empty() &&
			loaded.Frames() <= maxFrames &&
			!loaded.layers.empty();

		for (const Layer& layer : loaded.layers)
		{
			whole = whole && layer.cels.size() == loaded.durations.size();

			for (const Image& cel : layer.cels)
				whole = whole && cel.width == loaded.width && cel.height == loaded.height;
		};

		if (!whole)
		{
			std::cerr << "Sprites: " << path << " is not a whole sprite" << std::endl;
			return false;
		};

		if (found.image.empty())
			found.image = BesideAsPng(path);

		sprite = std::move(loaded);
		links = std::move(found);

		return true;
	};

	bool SpriteAsset::Save(const std::string& path, const SpriteAsset& sprite, const Links& links)
	{
		const std::filesystem::path folder = std::filesystem::path(path).parent_path();

		std::ostringstream out;

		out << image_key << separator << Relative(links.image, folder) << '\n';

		if (!links.palette.empty())
			out << palette_key << separator << Relative(links.palette, folder) << '\n';

		out << size_key << separator << sprite.width << size_between << sprite.height << '\n';

		for (const int duration : sprite.durations)
			out << frame_key << separator << duration << '\n';

		for (const Layer& layer : sprite.layers)
		{
			out << layer_key << separator << layer.name << '\n';

			if (!layer.visible)
				out << hidden_key << '\n';

			if (layer.outlined)
				out << outlined_key << '\n';

			for (const Image& cel : layer.cels)
			{
				out << cel_key << separator;

				if (!cel.IsClear())
				{
					const std::vector<char> png = Encode(".png", cel);

					if (png.empty())
					{
						std::cerr << "Sprites: could not encode " << path << std::endl;
						return false;
					};

					out << McpProtocol::Base64(png);
				};

				out << '\n';
			};
		};

		return WriteFile(path, out.str(), "Sprites");
	};

	#pragma endregion

	#pragma region Editing

	SpriteAsset SpriteAsset::FromImage(Image image)
	{
		SpriteAsset sprite;
		sprite.width = image.width;
		sprite.height = image.height;
		sprite.durations = { defaultDuration };
		sprite.layers = { Layer{ "Layer 1", true, { std::move(image) } } };

		return sprite;
	};

	void SpriteAsset::AddLayer(int at, const std::string& name)
	{
		layers.insert(layers.begin() + at, Layer{ name, true, std::vector<Image>(durations.size(), Image::Blank(width, height)) });
	};

	void SpriteAsset::MoveLayer(int from, int to)
	{
		const auto begin = layers.begin();

		if (from < to)
			std::rotate(begin + from, begin + from + 1, begin + to + 1);
		else
			std::rotate(begin + to, begin + from, begin + from + 1);
	};

	void SpriteAsset::TakeIn(Image image, const std::string& name)
	{
		if (image.width != width * Frames() || image.height != height)
		{
			*this = FromImage(std::move(image));
			return;
		};

		AddLayer((int)layers.size(), name);

		const size_t row = (size_t)width * channels;

		for (int frame = 0; frame < Frames(); frame++)
			for (int y = 0; y < height; y++)
				std::copy_n(
					image.pixels.begin() + (ptrdiff_t)(((size_t)y * image.width + (size_t)frame * width) * channels),
					row,
					layers.back().cels[frame].pixels.begin() + (ptrdiff_t)(y * row));
	};

	void SpriteAsset::AddFrame(int at, bool copy_previous)
	{
		const bool copy = copy_previous && at > 0;

		durations.insert(durations.begin() + at, copy ? durations[at - 1] : defaultDuration);

		for (Layer& layer : layers)
			layer.cels.insert(layer.cels.begin() + at, copy ? layer.cels[at - 1] : Image::Blank(width, height));
	};

	void SpriteAsset::RemoveFrame(int frame)
	{
		durations.erase(durations.begin() + frame);

		for (Layer& layer : layers)
			layer.cels.erase(layer.cels.begin() + frame);
	};

	void SpriteAsset::Resize(int to_width, int to_height, bool scale, float anchor_x, float anchor_y)
	{
		const int left = (int)std::floor((to_width - width) * anchor_x);
		const int top = (int)std::floor((to_height - height) * anchor_y);

		for (Layer& layer : layers)
			for (Image& cel : layer.cels)
				cel = scale ? cel.Scaled(to_width, to_height) : cel.Placed(to_width, to_height, left, top);

		width = to_width;
		height = to_height;
	};

	#pragma endregion

	#pragma region Contents

	SpriteAsset::Image SpriteAsset::Flatten(int frame) const
	{
		Image image = Image::Blank(width, height);

		for (const Layer& layer : layers)
			if (layer.visible)
				image.Over(layer.cels[frame]);

		return image;
	};

	SpriteAsset::Image SpriteAsset::Sheet() const
	{
		Image sheet = Image::Blank(width * Frames(), height);

		const size_t row = (size_t)width * channels;

		for (int frame = 0; frame < Frames(); frame++)
		{
			const Image flat = Flatten(frame);

			for (int y = 0; y < height; y++)
				std::copy_n(
					flat.pixels.begin() + (ptrdiff_t)(y * row),
					row,
					sheet.pixels.begin() + (ptrdiff_t)(((size_t)y * sheet.width + (size_t)frame * width) * channels));
		};

		return sheet;
	};

	std::vector<SpriteAsset::Colour> SpriteAsset::Colours(size_t limit) const
	{
		std::vector<Colour> colours;
		std::unordered_set<uint32_t> met;

		for (const Layer& layer : layers)
		{
			if (!layer.visible)
				continue;

			for (const Image& cel : layer.cels)
				for (int y = 0; y < cel.height; y++)
					for (int x = 0; x < cel.width; x++)
					{
						const Colour colour = cel.Get(x, y);
						uint32_t packed = 0;
						std::memcpy(&packed, colour.data(), sizeof(packed));

						if (colour[alpha] == 0 || !met.insert(packed).second)
							continue;

						colours.push_back(colour);

						if (colours.size() >= limit)
							return colours;
					};
		};

		return colours;
	};

	size_t SpriteAsset::Bytes() const
	{
		size_t bytes = 0;

		for (const Layer& layer : layers)
			for (const Image& cel : layer.cels)
				bytes += cel.pixels.size();

		return bytes;
	};

	#pragma endregion

	#pragma region Palette

	bool Palette::Load(const std::string& path, Palette& palette)
	{
		std::ifstream file(path);

		if (!file)
		{
			std::cerr << "Palette: could not open " << path << std::endl;
			return false;
		};

		Palette loaded;
		std::string line;

		while (std::getline(file, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			if (line.size() > 1 && line.front() == category_open && line.back() == category_close)
			{
				loaded.categories.push_back({ line.substr(1, line.size() - 2), { } });
				continue;
			};

			// #RRGGBB is opaque.
			if (line.empty() || line[0] != colour_mark || (line.size() != 1 + hex_digits * rgb && line.size() != 1 + hex_digits * channels))
				continue;

			SpriteAsset::Colour colour = { 0, 0, 0, UINT8_MAX };
			bool read = true;

			for (size_t c = 0; c < (line.size() - 1) / hex_digits; c++)
			{
				const char* begin = line.data() + 1 + c * hex_digits;
				const auto [end, error] = std::from_chars(begin, begin + hex_digits, colour[c], hex);

				read = read && error == std::errc() && end == begin + hex_digits;
			};

			if (read)
				(loaded.categories.empty() ? loaded.colours : loaded.categories.back().colours).push_back(colour);
		};

		palette = std::move(loaded);

		return true;
	};

	bool Palette::Save(const std::string& path, const Palette& palette)
	{
		constexpr const char* format = "#%02X%02X%02X%02X\n";
		constexpr size_t line_size = sizeof("#RRGGBBAA\n");

		std::string text;

		const auto write = [&text](const std::vector<SpriteAsset::Colour>& colours)
		{
			for (const SpriteAsset::Colour& colour : colours)
			{
				char line[line_size];
				snprintf(line, sizeof(line), format, colour[0], colour[1], colour[2], colour[3]);
				text += line;
			};
		};

		write(palette.colours);

		for (const Palette::Category& category : palette.categories)
		{
			text += category_open + category.name + category_close + '\n';
			write(category.colours);
		};

		return WriteFile(path, text, "Palette");
	};

	#pragma endregion
};
