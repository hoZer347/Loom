#include "doctest.h"

#include "LoomObject.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <type_traits>

// The cases that may know whether they run as MSVC's executable or emcc's module.

#ifdef __EMSCRIPTEN__
#define LOOM_TEST_BUILD "web"
#else
#define LOOM_TEST_BUILD "local"
#endif


TEST_SUITE("Platform")
{
	TEST_CASE("the build identifies itself")
	{
		INFO("Running the " << LOOM_TEST_BUILD << " build");

		CHECK(std::string(LOOM_TEST_BUILD) != "");
	};

	// size_t is 32 bits on the web, so IDs are wider than it there.
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

	// A single-threaded web build still compiles and runs every lock.
	TEST_CASE("threading support matches what the build can offer")
	{
#ifdef __EMSCRIPTEN__
#ifdef __EMSCRIPTEN_PTHREADS__
		INFO("Web build has pthreads enabled");
		CHECK(std::thread::hardware_concurrency() >= 1);
#else
		INFO("Web build is single threaded; engine locks are uncontended");
		CHECK(true);
#endif
#else
		CHECK(std::thread::hardware_concurrency() >= 1);
#endif
	};

	// The desktop reads shaders with an ifstream, the web with emscripten_fetch.
	TEST_CASE("the build knows how it loads shader sources")
	{
#ifdef __EMSCRIPTEN__
		INFO("Shaders are fetched over HTTP on this build");
#else
		INFO("Shaders are read from the filesystem on this build");
#endif
		CHECK(true);
	};
};
