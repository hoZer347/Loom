#include "doctest.h"

#include "Shaders.h"

#include <stdexcept>
#include <string>
#include <type_traits>


TEST_SUITE("Shader")
{
	// Shader compiles on construction, so most of it needs a GL context. What
	// does not is the source-loading step in front of the compiler, which is
	// where a wrong path or a malformed shader file gets caught.

	TEST_CASE("the shader registry starts out without a bogus path in it")
	{
		CHECK_FALSE(Loom::Shader::shaders.contains("no such shader.shader"));
	};

#ifndef __EMSCRIPTEN__
	// The native build reads shader sources off disk with an ifstream, and
	// fails before it ever reaches glCreateProgram -- so this is reachable with
	// no window open. The web build fetches over HTTP instead, which needs a
	// browser, so the same case is skipped there.
	TEST_CASE("constructing a Shader from a missing file throws")
	{
		CHECK_THROWS_AS(
			Loom::Shader("this file does not exist.shader"),
			std::runtime_error);
	};

	TEST_CASE("a failed compile does not leave the path in the registry")
	{
		const std::string missing = "another missing shader.shader";

		try { Loom::Shader shader(missing); }
		catch (const std::runtime_error&) { };

		CHECK_FALSE(Loom::Shader::shaders.contains(missing));
	};

	// The one-argument constructor appends the extension for you, so callers can
	// say Shader("Loom") instead of Shader("Loom.shader").
	//
	// It only half works today: file_path is normalised, but the id is compiled
	// from the raw constructor argument rather than from the normalised member,
	// so "Loom" and "Loom.shader" are looked up as two different shaders. Both
	// paths fail the same way here, which is all this can check without a
	// context.
	TEST_CASE("both spellings of a shader path fail the same way when the file is missing")
	{
		CHECK_THROWS_AS(Loom::Shader("missing"), std::runtime_error);
		CHECK_THROWS_AS(Loom::Shader("missing.shader"), std::runtime_error);
	};
#endif

	TEST_CASE("Shader is a plain resource handle, not a component")
	{
		// Materials hold a Shader*; a shader is not attached to a GameObject and
		// has no lifecycle callbacks of its own.
		CHECK(std::is_final_v<Loom::Shader>);

		// Its const members make it non-assignable, but the compiler still
		// generates a copy constructor -- and ~Shader calls glDeleteShader, so a
		// copy would delete the same GL program twice. Shaders are meant to be
		// held by pointer; this records that copying one is not defended against.
		CHECK(std::is_copy_constructible_v<Loom::Shader>);
		CHECK_FALSE(std::is_copy_assignable_v<Loom::Shader>);
	};
};
