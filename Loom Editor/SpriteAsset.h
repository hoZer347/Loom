#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::SpriteAsset
	* - What the Sprites panel edits: frames of animation, each the same size,
	*   drawn in layers that are stacked bottom first, any of which can be
	*   hidden
	* - Kept in a .loomsprite file beside the image it is flattened into, under
	*   the image's stem. Saving writes both: the sprite for the editor, and
	*   the image, its frames side by side, for whatever draws it
	* - The file is lines of name=value: image= and palette= relative to it,
	*   size=WxH, a frame= of milliseconds for each frame, then a layer= of
	*   each layer's name, a hidden line when it is, an outlined line when
	*   the editor outlines it, and a cel= for each frame of its pixels as
	*   base64 PNG, or nothing when they are all clear. With no image= line,
	*   the image is the .png beside it under its stem
	*/
	struct SpriteAsset final
	{
		static constexpr const char* extension = ".loomsprite";
		static constexpr int defaultDuration = 100;

		// What a frame's milliseconds are held to, in a file too.
		static constexpr int minDuration = 1;
		static constexpr int maxDuration = 10000;

		// As many as the Sprites panel's timeline has columns for.
		static constexpr int maxFrames = 511;

		// Bounds what one cel allocates.
		static constexpr int maxSide = 16384;

		using Colour = std::array<uint8_t, 4>;

		// Eight bit RGBA, top row first.
		struct Image
		{
			int width = 0;
			int height = 0;
			std::vector<uint8_t> pixels;

			// All clear.
			static Image Blank(int width, int height);

			bool Contains(int x, int y) const;
			Colour Get(int x, int y) const;

			// Off the image does nothing.
			void Set(int x, int y, const Colour& colour);

			// Every pixel from one end to the other, both included, so a stroke
			// has no gaps however fast the mouse went.
			void Line(int x0, int y0, int x1, int y1, const Colour& colour);

			// The pixel and every one joined to it side by side through pixels
			// of its colour.
			void Fill(int x, int y, const Colour& colour);

			bool IsClear() const;

			// Laid over this one where they meet, see-through as its alpha says.
			void Over(const Image& above);

			bool operator==(const Image&) const = default;
		};

		struct Layer
		{
			std::string name;
			bool visible = true;

			// One for each frame.
			std::vector<Image> cels;

			// Whether the Sprites panel draws round its pixels, to show what is
			// on it. Nothing in the image changes.
			bool outlined = false;

			bool operator==(const Layer&) const = default;
		};

		// Where the sprite's own file is linked to.
		struct Links
		{
			// Absolute.
			std::string image;

			// Absolute, or empty for none.
			std::string palette;

			bool operator==(const Links&) const = default;
		};

		// Each says what went wrong on std::cerr.
		static bool LoadImage(const std::string& path, Image& image);
		static bool SaveImage(const std::string& path, const Image& image);

		// The image as SaveImage would leave it at path, losses and all, so
		// one read back can be told from one written by something else.
		static Image AsSaved(const std::string& path, const Image& image);

		// The file SaveImage writes for an extension, such as ".png". Empty for
		// one it cannot write.
		static std::vector<char> Encode(const std::string& extension, const Image& image);

		// One frame of one layer, the whole image.
		static SpriteAsset FromImage(Image image);

		// The sprite kept for an image: beside it, under its stem.
		static std::string For(const std::string& image_path);

		// The image a sprite file is flattened into, read from its image= line
		// without decoding the rest, or the .png beside it when it names none.
		// Empty when it will not open.
		static std::string ImageOf(const std::string& path);

		// The other of a sprite and the image it is flattened into, when both
		// are in one folder, so a move can take them together. Empty for a
		// file that has none.
		static std::string Companion(const std::string& path);

		static bool Load(const std::string& path, SpriteAsset& sprite, Links& links);
		static bool Save(const std::string& path, const SpriteAsset& sprite, const Links& links);

		int Frames() const { return (int)durations.size(); };

		// The visible layers of a frame, each over those under it.
		Image Flatten(int frame) const;

		// Every frame flattened, side by side, the first on the left.
		Image Sheet() const;

		// Every colour any visible layer of any frame holds, but clear, in the
		// order first met, at most limit of them.
		std::vector<Colour> Colours(size_t limit) const;

		void AddLayer(int at, const std::string& name);

		// Takes a layer out and puts it back at to, those between moving over
		// to make room.
		void MoveLayer(int from, int to);

		// A new top layer of the image, cut into the frames when it is the
		// size of the sheet. One of any other size starts the sprite over as
		// it.
		void TakeIn(Image image, const std::string& name);
		void AddFrame(int at, bool copy_previous);
		void RemoveFrame(int frame);

		// Pixels of every cel, so undo can keep fewer of a large sprite.
		size_t Bytes() const;

		int width = 0;
		int height = 0;

		// How long each frame shows, in milliseconds.
		std::vector<int> durations;

		// The bottom first.
		std::vector<Layer> layers;

		bool operator==(const SpriteAsset&) const = default;
	};

	/**
	* Loom::Palette
	* - The colours a sprite is painted from, in a .loompalette file of its
	*   own, a #RRGGBBAA line each, so sprites can share one
	*/
	struct Palette final
	{
		static constexpr const char* extension = ".loompalette";

		std::vector<SpriteAsset::Colour> colours;

		// Each says what went wrong on std::cerr. A line that is not a colour
		// is skipped.
		static bool Load(const std::string& path, Palette& palette);
		static bool Save(const std::string& path, const Palette& palette);

		bool operator==(const Palette&) const = default;
	};
};
