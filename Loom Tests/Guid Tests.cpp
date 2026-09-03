#include "doctest.h"

#include "Test Support.h"

#include "Guid.h"
#include "LoomObject.h"
#include "Scene.h"

#include <set>
#include <string>

using LoomTests::Pump;


TEST_SUITE("Guid")
{
	TEST_CASE("a default guid is not a guid")
	{
		const Loom::Guid guid;

		CHECK_FALSE(guid.IsValid());
		CHECK(guid.high == 0);
		CHECK(guid.low == 0);
	};

	TEST_CASE("New hands out valid, distinct guids")
	{
		std::set<std::string> seen;

		for (int i = 0; i < 512; i++)
		{
			const Loom::Guid guid = Loom::Guid::New();

			CHECK(guid.IsValid());

			seen.insert(guid.ToString());
		};

		CHECK(seen.size() == 512);
	};

	// Written into scene files, so the text form has to be the shape everything
	// else expects of a guid.
	TEST_CASE("the text form is the canonical 8-4-4-4-12")
	{
		const std::string text = Loom::Guid::New().ToString();

		REQUIRE(text.size() == 36);

		CHECK(text[8] == '-');
		CHECK(text[13] == '-');
		CHECK(text[18] == '-');
		CHECK(text[23] == '-');

		// Version 4, and the RFC 4122 variant.
		CHECK(text[14] == '4');
		CHECK((text[19] == '8' || text[19] == '9' || text[19] == 'a' || text[19] == 'b'));
	};

	TEST_CASE("a guid survives being written and read back")
	{
		const Loom::Guid guid = Loom::Guid::New();

		Loom::Guid parsed;

		REQUIRE(Loom::Guid::TryParse(guid.ToString(), parsed));

		CHECK(parsed == guid);
		CHECK(parsed.ToString() == guid.ToString());
	};

	TEST_CASE("the dashes are optional when reading")
	{
		const Loom::Guid guid = Loom::Guid::New();

		std::string bare;

		for (const char c : guid.ToString())
			if (c != '-')
				bare += c;

		Loom::Guid parsed;

		REQUIRE(Loom::Guid::TryParse(bare, parsed));

		CHECK(parsed == guid);
	};

	TEST_CASE("anything that is not a guid is refused")
	{
		Loom::Guid parsed = Loom::Guid::New();

		const char* const rejected[] =
		{
			"",
			"not a guid",
			"0f8e1b3a-2c4d-5e6f",                       // too short
			"0f8e1b3a-2c4d-4e6f-8a1b-2c3d4e5f6a7b8c",   // too long
			"gggggggg-2c4d-4e6f-8a1b-2c3d4e5f6a7b",     // not hex
		};

		const Loom::Guid untouched = parsed;

		for (const char* text : rejected)
			CHECK_FALSE(Loom::Guid::TryParse(text, parsed));

		// And a refused read leaves the guid it was given exactly as it was,
		// rather than half-written from the characters it did understand.
		CHECK(parsed == untouched);
	};

	// Equality and the hash are what the lookup table is built on.
	TEST_CASE("guids compare by value")
	{
		const Loom::Guid a(1, 2);
		const Loom::Guid b(1, 2);
		const Loom::Guid c(1, 3);

		CHECK(a == b);
		CHECK(a != c);
		CHECK(std::hash<Loom::Guid>{ }(a) == std::hash<Loom::Guid>{ }(b));
		CHECK(std::hash<Loom::Guid>{ }(a) != std::hash<Loom::Guid>{ }(c));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "every object carries one, and can be found by it")
	{
		Loom::Scene scene("guid lookup");
		Pump();

		const Loom::Guid guid = scene.GetGuid();

		CHECK(guid.IsValid());
		CHECK(Loom::LoomObject::GetByGuid(guid) == &scene);

		// Loading a scene file puts objects back under the guid they were saved
		// with, so the table has to follow.
		const Loom::Guid replacement = Loom::Guid::New();

		scene.SetGuid(replacement);

		CHECK(scene.GetGuid() == replacement);
		CHECK(Loom::LoomObject::GetByGuid(replacement) == &scene);
		CHECK(Loom::LoomObject::GetByGuid(guid) == nullptr);
	};

	// Two objects cannot answer to one guid: the second claim wins, and the
	// first object losing it must not take the entry with it when it dies.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a guid claimed twice belongs to the claimant")
	{
		Loom::Scene keeper("keeper");
		Pump();

		const Loom::Guid contested = Loom::Guid::New();

		{
			Loom::Scene loser("loser");
			Pump();

			loser.SetGuid(contested);
			CHECK(Loom::LoomObject::GetByGuid(contested) == &loser);

			keeper.SetGuid(contested);
			CHECK(Loom::LoomObject::GetByGuid(contested) == &keeper);
		};

		Pump();

		CHECK(Loom::LoomObject::GetByGuid(contested) == &keeper);
	};
};
