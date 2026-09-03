#include "doctest.h"

#include "Textures.h"

#include <type_traits>


TEST_SUITE("Texture")
{
	// Texture has no members and no behaviour yet -- Material holds a Shader and
	// nothing samples anything. These cases pin the type down so that giving it
	// a GL handle, a loader or a lifetime has a place to be tested from, and so
	// that "textures are not implemented" is a stated fact rather than a gap.

	TEST_CASE("Texture exists and is default constructible")
	{
		Loom::Texture texture;
		(void)texture;

		CHECK(std::is_default_constructible_v<Loom::Texture>);
	};

	TEST_CASE("Texture carries no state yet")
	{
		CHECK(std::is_empty_v<Loom::Texture>);
		CHECK(std::is_final_v<Loom::Texture>);
	};

	TEST_CASE("Texture is not a component")
	{
		// Like Shader, a texture is a resource a Material points at rather than
		// something attached to a GameObject.
		CHECK(std::is_trivially_copyable_v<Loom::Texture>);
	};
};
