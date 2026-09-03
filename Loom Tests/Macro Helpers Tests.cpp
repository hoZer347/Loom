#include "doctest.h"

#include "Macro Helpers.h"

#include <string>
#include <type_traits>
#include <vector>

// HAS_FUNCTION_DECL / HAS_VARIABLE_DECL have to be expanded at namespace scope,
// which is why these sit above the test suite rather than inside a case.
HAS_FUNCTION_DECL(push_back)
HAS_FUNCTION_DECL(clear)
HAS_FUNCTION_DECL(no_such_function)

HAS_VARIABLE_DECL(size)
HAS_VARIABLE_DECL(no_such_member)

namespace
{
	struct WithMember
	{
		int size = 3;
	};

	struct WithNothing
	{ };

	template <Str NAME>
	const char* Tagged()
	{
		return NAME.value;
	};
};


TEST_SUITE("Macro Helpers")
{
	TEST_CASE("HAS_FUNCTION_TEST finds a callable member")
	{
		CHECK(HAS_FUNCTION_TEST(std::vector<int>, clear));
		CHECK(HAS_FUNCTION_TEST(std::string, clear));
	};

	TEST_CASE("HAS_FUNCTION_TEST reports false for a member that is not there")
	{
		CHECK_FALSE(HAS_FUNCTION_TEST(std::vector<int>, no_such_function));
		CHECK_FALSE(HAS_FUNCTION_TEST(WithNothing, clear));
	};

	// The check calls the member with no arguments, so a member that needs one
	// reads as absent. Worth knowing before reaching for this on a setter.
	TEST_CASE("HAS_FUNCTION_TEST only sees members callable with no arguments")
	{
		CHECK_FALSE(HAS_FUNCTION_TEST(std::vector<int>, push_back));
	};

	TEST_CASE("HAS_VARIABLE_TEST finds a data member")
	{
		CHECK(HAS_VARIABLE_TEST(WithMember, size));
		CHECK_FALSE(HAS_VARIABLE_TEST(WithMember, no_such_member));
		CHECK_FALSE(HAS_VARIABLE_TEST(WithNothing, size));
	};

	// Str exists so a string literal can be a template argument.
	TEST_CASE("Str carries a literal through a template parameter")
	{
		CHECK(std::string(Tagged<"Hello World">()) == "Hello World");
		CHECK(std::string(Tagged<"">()).empty());
	};

	TEST_CASE("Str keeps the terminator, so its size is one more than the text")
	{
		constexpr Str text{ "abc" };

		CHECK(sizeof(text.value) == 4);
		CHECK(text.value[3] == '\0');
	};

	TEST_CASE("VARIABLE_NAME stringifies its argument")
	{
		int someVariable = 0;
		(void)someVariable;

		CHECK(std::string(VARIABLE_NAME(someVariable)) == "someVariable");
		CHECK(std::string(VARIABLE_NAME(Loom::Engine)) == "Loom::Engine");
	};
};
