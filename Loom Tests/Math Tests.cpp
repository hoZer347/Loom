#include "doctest.h"

#include "Vector.h"
#include "Globals.h"

#include <vector>

using namespace Loom::Math;


TEST_SUITE("Loom Math")
{
	TEST_CASE("vec3 stores its components in declaration order")
	{
		vec3<float> v{ 1.0f, 2.0f, 3.0f };

		CHECK(v.data[0] == doctest::Approx(1.0f));
		CHECK(v.data[1] == doctest::Approx(2.0f));
		CHECK(v.data[2] == doctest::Approx(3.0f));
	};

	TEST_CASE("vec3 works with integral component types")
	{
		vec3<int> v{ -4, 0, 7 };

		CHECK(v.data[0] == -4);
		CHECK(v.data[1] == 0);
		CHECK(v.data[2] == 7);
	};

	TEST_CASE("vec3 defaults its component type to float")
	{
		vec3<> v{ 0.5f, 0.5f, 0.5f };

		CHECK(sizeof(v.data[0]) == sizeof(float));
	};

	// The vector types exist to be handed straight to GL, so their size has to
	// match the packed layout the driver expects.
	TEST_CASE("vector types are tightly packed")
	{
		CHECK(sizeof(vec2<float>) == 2 * sizeof(float));
		CHECK(sizeof(vec3<float>) == 3 * sizeof(float));
		CHECK(sizeof(vec4<float>) == 4 * sizeof(float));
		CHECK(sizeof(mat4<float>) == 16 * sizeof(float));
	};

	TEST_CASE("vector types stay packed for wider components")
	{
		CHECK(sizeof(vec2<double>) == 2 * sizeof(double));
		CHECK(sizeof(vec4<double>) == 4 * sizeof(double));
		CHECK(sizeof(mat4<double>) == 16 * sizeof(double));
	};

	// vec2, vec4 and mat4 carry an explicit alignas so they can be uploaded
	// without padding; vec3 deliberately does not, which is why it is the odd
	// one out here.
	TEST_CASE("aligned vector types honour their alignas")
	{
		CHECK(alignof(vec2<float>) == 2 * sizeof(float));
		CHECK(alignof(vec4<float>) == 4 * sizeof(float));
		CHECK(alignof(mat4<float>) == 16 * sizeof(float));
	};

	TEST_CASE("vec3 takes the natural alignment of its component")
	{
		CHECK(alignof(vec3<float>) == alignof(float));
	};

	TEST_CASE("a mat4 holds its 16 elements contiguously")
	{
		mat4<float> m{ };

		for (int i = 0; i < 16; i++)
			m.data[i] = static_cast<float>(i);

		const float* raw = &m.data[0];

		for (int i = 0; i < 16; i++)
			CHECK(raw[i] == doctest::Approx(static_cast<float>(i)));
	};

	TEST_CASE("the global transform is an identity matrix translated on x")
	{
		REQUIRE(Loom::transform.size() == 16);

		const std::vector<float> expected =
		{
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.0f, 0.0f, 1.0f,
		};

		for (size_t i = 0; i < expected.size(); i++)
			CHECK(Loom::transform[i] == doctest::Approx(expected[i]));
	};

	// Written as four rows of four with the translation in the last row, which
	// is the column-major layout GL expects from a uniform upload.
	TEST_CASE("the translation sits where a column-major upload expects it")
	{
		REQUIRE(Loom::transform.size() == 16);

		CHECK(Loom::transform[12] == doctest::Approx(0.5f));
		CHECK(Loom::transform[13] == doctest::Approx(0.0f));
		CHECK(Loom::transform[14] == doctest::Approx(0.0f));
		CHECK(Loom::transform[15] == doctest::Approx(1.0f));
	};
};
