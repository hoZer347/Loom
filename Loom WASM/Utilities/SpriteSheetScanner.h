#pragma once

#include <vector>


namespace Loom
{
	struct Rect final
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;

		bool IsZero() const { return width == 0.0f && height == 0.0f; };

		static Rect Zero() { return Rect{ }; };
	};

	struct Int2 final
	{
		int x = 0;
		int y = 0;

		bool operator==(const Int2& rhs) const { return x == rhs.x && y == rhs.y; };
	};

	struct Float2 final
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	/// A sheet's pixels, as tightly-packed RGBA bytes.
	///
	/// There is no texture importer, so the caller passes whatever it decoded the file
	/// into. Row 0 is the BOTTOM row, matching how a GL texture is uploaded; the
	/// "clip 0 is the top of the sheet" flip below depends on it.
	struct PixelSheet final
	{
		const unsigned char* pixels = nullptr;
		int width = 0;
		int height = 0;

		bool IsReadable() const { return pixels != nullptr && width > 0 && height > 0; };
	};

	/// Works out how long each clip on a sprite sheet is by looking at the sheet itself.
	///
	/// The sheet is walked as a grid of cells; a cell counts as empty when every pixel in
	/// it is transparent. Each row's length is the column of its last non-empty cell plus
	/// one -- measuring from the END rather than stopping at the first gap, so a
	/// deliberate blank frame mid-animation survives while trailing padding does not.
	struct SpriteSheetScanner final
	{
		/// Frame count per clip, one entry per row of the sheet, top row first. Rows that
		/// are entirely blank come back as 0.
		///
		/// Empty when the sheet cannot be read, or when the sheet and cell size simply do
		/// not describe a grid.
		static std::vector<int> Measure(
			const PixelSheet& sheet,
			const Int2& cell,
			unsigned char alphaThreshold = 1);

		/// Where the art actually sits inside a cell, as a rect in normalised cell space
		/// with its origin at the cell's bottom-left. Taken as the union across every cell,
		/// so it holds for each frame of every clip rather than the one on screen.
		///
		/// Only the vertical half of this is worth pivoting on. Because it is a union, a
		/// clip that lunges to one side widens the box and drags its middle off the cell's
		/// middle, which is not where "centred" should mean.
		///
		/// Sheets pad their frames, and that padding scales along with the sprite.
		/// Anchoring the quad on the cell edge therefore lifts a scaled-up sprite off the
		/// ground by however much padding it had. Anchoring it on this box keeps the feet
		/// planted at any size, without anyone having to dial the pivot in by hand.
		///
		/// Zero when the sheet cannot be read, or holds no ink.
		static Rect MeasureContent(
			const PixelSheet& sheet,
			const Int2& cell,
			unsigned char alphaThreshold = 1);

		/// As MeasureContent, but one box per clip -- the union across just that row's
		/// cells, top row first. Rows with no ink come back as zero.
		///
		/// The sheet-wide box is anchored to the lowest frame anywhere, so a clip that
		/// never reaches that low sits visibly above the ground. Per-clip boxes plant every
		/// clip on its own lowest pixel; the cost is that the sprite steps vertically when
		/// the clip changes, which is why it is a choice rather than the default.
		static std::vector<Rect> MeasureClipContent(
			const PixelSheet& sheet,
			const Int2& cell,
			unsigned char alphaThreshold = 1);

		SpriteSheetScanner() = delete;
	};
};
