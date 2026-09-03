#include "doctest.h"

#include "Test Support.h"

#include "LoomObject.h"

#include <set>
#include <string>

using LoomTests::Pump;

namespace
{
	// LoomObject is abstract only in spirit -- it has no pure virtuals -- but
	// everything real derives from it, so the tests use a minimal stand-in.
	struct Thing : Loom::LoomObject
	{ };
};


TEST_SUITE("LoomObject")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "every object gets its own ID")
	{
		std::set<Loom::uint64_t> ids;

		{
			Thing a, b, c;
			Pump();

			CHECK(ids.insert(a.m_ID).second);
			CHECK(ids.insert(b.m_ID).second);
			CHECK(ids.insert(c.m_ID).second);
		};

		Pump();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a fresh object has an empty name")
	{
		Thing thing;
		Pump();

		CHECK(thing.GetName().empty());

		Pump();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "SetName replaces the name")
	{
		Thing thing;
		Pump();

		thing.SetName("first");
		CHECK(thing.GetName() == "first");

		thing.SetName("second");
		CHECK(thing.GetName() == "second");

		Pump();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "NameAndID is the display label used by the editor")
	{
		Thing thing;
		Pump();

		thing.SetName("Player");

		CHECK(thing.NameAndID() == "Player (ID: " + std::to_string(thing.m_ID) + ')');

		Pump();
	};

	// Initialize() queues the registration, but the queued task bails out with
	//
	//     if (by_name.contains(NameAndID())) return;
	//
	// before it ever reaches by_id.emplace. SetName has already put NameAndID()
	// into by_name by then, so an object named before the first drain -- which is
	// every GameObject and every Scene, both of which SetName in their
	// constructor -- never makes it into the ID table at all.
	//
	// Marked should_fail rather than deleted: doctest reports it as a failure the
	// day it starts passing, which is the signal to drop the marker.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "GetByID finds a registered object" * doctest::should_fail())
	{
		Thing thing;
		thing.SetName("Findable");

		// Registration is queued, not immediate -- before the pump the lookup
		// table does not know about the object yet.
		Pump();

		CHECK(Loom::LoomObject::GetByID(thing.m_ID) == &thing);

		Pump();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "GetByID returns null for an ID that was never issued")
	{
		// _GetByID indexes the map with operator[], so a miss inserts a null
		// entry rather than leaving the table alone. The result is still null,
		// which is what a caller checks, but the table grows on every miss.
		CHECK(Loom::LoomObject::GetByID(~Loom::uint64_t(0)) == nullptr);

		Pump();
	};

	// Blocked by the same registration bug: the object never reaches by_id, so
	// there is nothing to observe being removed from it.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "destroying an object unregisters its ID" * doctest::should_fail())
	{
		Loom::uint64_t id = 0;

		{
			Thing thing;
			thing.SetName("Temporary");
			Pump();

			id = thing.m_ID;
			REQUIRE(Loom::LoomObject::GetByID(id) == &thing);
		};

		Pump();

		CHECK(Loom::LoomObject::GetByID(id) == nullptr);
	};

	// Blocked by the same registration bug.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "GetByID casts to the requested type" * doctest::should_fail())
	{
		Thing thing;
		thing.SetName("Typed");
		Pump();

		const Thing* found = Loom::LoomObject::GetByID<Thing>(thing.m_ID);

		CHECK(found == &thing);

		Pump();
	};
};
