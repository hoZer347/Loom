#include "doctest.h"

#include "WindowFrame.h"


TEST_SUITE("WindowFrame")
{
	using Hit = Loom::WindowFrame::Hit;
	using Rect = Loom::WindowFrame::Rect;

	constexpr float width = 1600.0f;
	constexpr float height = 900.0f;
	constexpr float border = 4.0f;
	constexpr Rect strip = { 600.0f, 0.0f, 1450.0f, 20.0f };

	Hit At(float x, float y, bool maximized = false)
	{
		return Loom::WindowFrame::HitTest(x, y, width, height, border, maximized, strip);
	};

	TEST_CASE("the edges and corners resize the window")
	{
		CHECK(At(1.0f, 400.0f) == Hit::Left);
		CHECK(At(width - 1.0f, 400.0f) == Hit::Right);
		CHECK(At(800.0f, 1.0f) == Hit::Top);
		CHECK(At(800.0f, height - 1.0f) == Hit::Bottom);
		CHECK(At(1.0f, 1.0f) == Hit::TopLeft);
		CHECK(At(width - 1.0f, 1.0f) == Hit::TopRight);
		CHECK(At(1.0f, height - 1.0f) == Hit::BottomLeft);
		CHECK(At(width - 1.0f, height - 1.0f) == Hit::BottomRight);
	};

	TEST_CASE("the drag strip is the caption, and the rest is the editor's")
	{
		CHECK(At(1000.0f, 10.0f) == Hit::Caption);
		CHECK(At(100.0f, 10.0f) == Hit::Client);
		CHECK(At(1500.0f, 10.0f) == Hit::Client);
		CHECK(At(1000.0f, 400.0f) == Hit::Client);
	};

	TEST_CASE("a maximized window has no edges to resize by")
	{
		CHECK(At(1.0f, 400.0f, true) == Hit::Client);
		CHECK(At(1000.0f, 1.0f, true) == Hit::Caption);
		CHECK(At(width - 1.0f, 1.0f, true) == Hit::Client);
	};

	TEST_CASE("an empty strip drags nothing")
	{
		CHECK(Loom::WindowFrame::HitTest(1000.0f, 10.0f, width, height, border, false, { }) == Hit::Client);
	};
};
