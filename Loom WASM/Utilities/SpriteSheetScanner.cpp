#include "SpriteSheetScanner.h"


namespace Loom
{
	// Alpha of one pixel. Four bytes per pixel, alpha last.
	static unsigned char AlphaAt(const PixelSheet& sheet, int x, int y)
	{
		return sheet.pixels[(size_t(y) * size_t(sheet.width) + size_t(x)) * 4 + 3];
	};

	/// True when every pixel of the cell at (x, y) is below the alpha floor.
	static bool IsEmpty(
		const PixelSheet& sheet,
		int x,
		int y,
		const Int2& cell,
		unsigned char alphaThreshold)
	{
		for (int row = y; row < y + cell.y; row++)
			for (int column = x; column < x + cell.x; column++)
				if (AlphaAt(sheet, column, row) >= alphaThreshold)
					return false;

		return true;
	};

	std::vector<int> SpriteSheetScanner::Measure(
		const PixelSheet& sheet,
		const Int2& cell,
		unsigned char alphaThreshold)
	{
		if (!sheet.IsReadable() || cell.x <= 0 || cell.y <= 0)
			return { };

		const int columns = sheet.width / cell.x;
		const int rows = sheet.height / cell.y;

		if (columns <= 0 || rows <= 0)
			return { };

		std::vector<int> lengths(size_t(rows), 0);

		for (int row = 0; row < rows; row++)
		{
			// Clip 0 is the top of the sheet, but pixel row 0 is the bottom of it.
			const int bottom = sheet.height - (row + 1) * cell.y;
			int last = -1;

			for (int column = 0; column < columns; column++)
				if (!IsEmpty(sheet, column * cell.x, bottom, cell, alphaThreshold))
					last = column;

			lengths[size_t(row)] = last + 1;
		};

		return lengths;
	};

	// The union of every inked pixel across a range of rows, as a box in normalised cell
	// space. Shared by the sheet-wide measure and the per-clip one, which differ only in
	// how many rows they fold together.
	static Rect MeasureRows(
		const PixelSheet& sheet,
		const Int2& cell,
		int firstRow,
		int rowCount,
		int columns,
		unsigned char alphaThreshold)
	{
		int left = cell.x;
		int right = -1;
		int bottom = cell.y;
		int top = -1;

		for (int row = firstRow; row < firstRow + rowCount; row++)
		{
			const int origin = sheet.height - (row + 1) * cell.y;

			for (int line = 0; line < cell.y; line++)
				for (int column = 0; column < columns; column++)
					for (int x = 0; x < cell.x; x++)
					{
						if (AlphaAt(sheet, column * cell.x + x, origin + line) < alphaThreshold)
							continue;

						if (x < left)
							left = x;

						if (x > right)
							right = x;

						if (line < bottom)
							bottom = line;

						if (line > top)
							top = line;
					};
		};

		if (right < 0)
			return Rect::Zero();

		return Rect{
			left / float(cell.x),
			bottom / float(cell.y),
			(right - left + 1) / float(cell.x),
			(top - bottom + 1) / float(cell.y) };
	};

	Rect SpriteSheetScanner::MeasureContent(
		const PixelSheet& sheet,
		const Int2& cell,
		unsigned char alphaThreshold)
	{
		if (!sheet.IsReadable() || cell.x <= 0 || cell.y <= 0)
			return Rect::Zero();

		const int columns = sheet.width / cell.x;
		const int rows = sheet.height / cell.y;

		if (columns <= 0 || rows <= 0)
			return Rect::Zero();

		return MeasureRows(sheet, cell, 0, rows, columns, alphaThreshold);
	};

	std::vector<Rect> SpriteSheetScanner::MeasureClipContent(
		const PixelSheet& sheet,
		const Int2& cell,
		unsigned char alphaThreshold)
	{
		if (!sheet.IsReadable() || cell.x <= 0 || cell.y <= 0)
			return { };

		const int columns = sheet.width / cell.x;
		const int rows = sheet.height / cell.y;

		if (columns <= 0 || rows <= 0)
			return { };

		std::vector<Rect> boxes;
		boxes.reserve(size_t(rows));

		for (int row = 0; row < rows; row++)
			boxes.push_back(MeasureRows(sheet, cell, row, 1, columns, alphaThreshold));

		return boxes;
	};
};
