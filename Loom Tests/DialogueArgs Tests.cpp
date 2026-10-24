#include "doctest.h"

#include "Utilities/DialogueArgs.h"


TEST_SUITE("DialogueArgs")
{
	TEST_CASE("numbers read as what they say")
	{
		int whole = 0;
		unsigned int count = 0;
		char letter = 0;
		float fraction = 0.0f;

		CHECK(Loom::DialogueArgs::TryCoerce(" -3 ", whole));
		CHECK(whole == -3);
		CHECK(Loom::DialogueArgs::TryCoerce("7", count));
		CHECK(count == 7);
		CHECK(Loom::DialogueArgs::TryCoerce("65", letter));
		CHECK(letter == 'A');
		CHECK(Loom::DialogueArgs::TryCoerce("0.5f", fraction));
		CHECK(fraction == 0.5f);
	};

	TEST_CASE("a number the type cannot hold, or no number at all, is refused and leaves it as it was")
	{
		constexpr int WHOLE = 5;
		constexpr unsigned int COUNT = 6;
		constexpr char LETTER = 'B';
		constexpr float FRACTION = 0.25f;

		int whole = WHOLE;
		unsigned int count = COUNT;
		char letter = LETTER;
		long long wide = WHOLE;
		float fraction = FRACTION;
		double precise = FRACTION;

		CHECK_FALSE(Loom::DialogueArgs::TryCoerce("4294967297", whole));
		CHECK_FALSE(Loom::DialogueArgs::TryCoerce("-1", count));
		CHECK_FALSE(Loom::DialogueArgs::TryCoerce("300", letter));
		CHECK_FALSE(Loom::DialogueArgs::TryCoerce("99999999999999999999", wide));
		CHECK_FALSE(Loom::DialogueArgs::TryCoerce("nan", fraction));
		CHECK_FALSE(Loom::DialogueArgs::TryCoerce("inf", fraction));
		CHECK_FALSE(Loom::DialogueArgs::TryCoerce("1e300", fraction));
		CHECK_FALSE(Loom::DialogueArgs::TryCoerce("-inf", precise));

		CHECK(whole == WHOLE);
		CHECK(count == COUNT);
		CHECK(letter == LETTER);
		CHECK(wide == WHOLE);
		CHECK(fraction == FRACTION);
		CHECK(precise == FRACTION);
	};
};
