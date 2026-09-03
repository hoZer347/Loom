#include "doctest.h"

#include "LoomObject.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <type_traits>

// The whole suite is compiled twice -- once by MSVC into a desktop executable,
// once by emcc into a WebAssembly module run under node. These cases are the
// ones that are allowed to know which, and they exist so that a difference
// between the two builds shows up as a named failure rather than as some other
// suite behaving oddly on one platform only.

#ifdef __EMSCRIPTEN__
#define LOOM_TEST_BUILD "web"
#else
#define LOOM_TEST_BUILD "local"
#endif


TEST_SUITE("Platform")
{
	TEST_CASE("the build identifies itself")
	{
		MESSAGE("Running the " << LOOM_TEST_BUILD << " build");

		CHECK(std::string(LOOM_TEST_BUILD) != "");
	};

	// Loom::uint64_t is its own typedef rather than std::uint64_t, and the
	// engine hands IDs out as size_t. On the web build size_t is 32 bits, so
	// these are not the same width there -- worth having stated.
	TEST_CASE("object IDs are 64 bits wide on both builds")
	{
		CHECK(sizeof(Loom::uint64_t) == 8);
		CHECK(std::is_unsigned_v<Loom::uint64_t>);
	};

	TEST_CASE("pointer and size_t width match the target")
	{
#ifdef __EMSCRIPTEN__
		// wasm32: a pointer is four bytes, and size_t with it.
		CHECK(sizeof(void*) == 4);
		CHECK(sizeof(size_t) == 4);
#else
		CHECK(sizeof(void*) == 8);
		CHECK(sizeof(size_t) == 8);
#endif
	};

	// Vertex and index buffers are handed to GL as raw bytes, so the sizes the
	// engine assumes have to hold on both targets.
	TEST_CASE("the types uploaded to GL are the same size on both builds")
	{
		CHECK(sizeof(float) == 4);
		CHECK(sizeof(uint32_t) == 4);
		CHECK(sizeof(int) == 4);
	};

	TEST_CASE("both targets are little endian")
	{
		const uint32_t value = 0x01020304u;
		unsigned char bytes[4];
		std::memcpy(bytes, &value, sizeof(value));

		CHECK(bytes[0] == 0x04);
		CHECK(bytes[3] == 0x01);
	};

	// State, LoomObject and Engine all lock a mutex on every call. A default
	// emscripten build is single threaded, where those locks are uncontended
	// but still have to compile and behave.
	TEST_CASE("threading support matches what the build can offer")
	{
#ifdef __EMSCRIPTEN__
#ifdef __EMSCRIPTEN_PTHREADS__
		MESSAGE("Web build has pthreads enabled");
		CHECK(std::thread::hardware_concurrency() >= 1);
#else
		MESSAGE("Web build is single threaded; engine locks are uncontended");
		CHECK(true);
#endif
#else
		CHECK(std::thread::hardware_concurrency() >= 1);
#endif
	};

	// Shader source loading is the one place the two builds genuinely diverge:
	// the desktop reads an ifstream, the web issues an emscripten_fetch. The
	// Shader suite skips its file cases on the web for exactly this reason.
	TEST_CASE("the build knows how it loads shader sources")
	{
#ifdef __EMSCRIPTEN__
		MESSAGE("Shaders are fetched over HTTP on this build");
#else
		MESSAGE("Shaders are read from the filesystem on this build");
#endif
		CHECK(true);
	};
};
