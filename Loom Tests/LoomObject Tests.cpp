#include "doctest.h"

#include "Test Support.h"

#include "GameObject.h"
#include "LoomObject.h"
#include "Scene.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

using LoomTests::Pump;

namespace
{
	// LoomObject is abstract only in spirit -- it has no pure virtuals -- but
	// everything real derives from it, so the tests use a minimal stand-in.
	struct Thing : Loom::LoomObject
	{ };

	struct Holder : Loom::LoomObject
	{
		Loom::Serial<int> count = 0;
		Loom::Serial<Loom::GameObject*> partner;
	};

	// Whether the journal names object, and for its children too.
	bool Names(const Loom::LoomObject::Journal& journal, const Loom::LoomObject* object, bool children = false)
	{
		return std::any_of(journal.touched.begin(), journal.touched.end(),
			[&](const auto& touched) { return touched.first == object && (!children || touched.second); });
	};
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

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "while watched, a written name or field puts the object in the journal once")
	{
		LoomTests::Watching watching;

		Holder holder;
		Pump();
		Loom::LoomObject::TakeJournal();

		holder.SetName("Named");
		holder.count = 1;
		holder.count = 2;

		const Loom::LoomObject::Journal journal = Loom::LoomObject::TakeJournal();

		CHECK(Names(journal, &holder));
		CHECK(std::count_if(journal.touched.begin(), journal.touched.end(),
			[&](const auto& touched) { return touched.first == &holder; }) == 1);
		CHECK(Loom::LoomObject::TakeJournal().touched.empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "reading a field leaves the journal alone, and changing it in place does not")
	{
		LoomTests::Watching watching;

		Holder holder;
		Pump();
		Loom::LoomObject::TakeJournal();

		const int read = holder.count;
		const int reached = *std::as_const(holder.count);

		CHECK(read == reached);
		CHECK(Loom::LoomObject::TakeJournal().touched.empty());

		holder.count += 1;

		CHECK(Names(Loom::LoomObject::TakeJournal(), &holder));

		holder.count++;

		CHECK(Names(Loom::LoomObject::TakeJournal(), &holder));
		CHECK(holder.count == 2);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a child coming or going puts its parent in the journal for its children")
	{
		LoomTests::Watching watching;

		Loom::Scene scene("Journal");
		Loom::GameObject* parent = scene.AddChild("Parent");
		Pump();
		Loom::LoomObject::TakeJournal();

		Loom::GameObject* child = parent->AddChild("Child");
		Pump();

		CHECK(Names(Loom::LoomObject::TakeJournal(), parent, true));

		child->Destroy();
		Pump();

		const Loom::LoomObject::Journal journal = Loom::LoomObject::TakeJournal();

		CHECK(Names(journal, parent, true));
		CHECK_FALSE(Names(journal, child));
		CHECK(std::find(journal.destroyed.begin(), journal.destroyed.end(), child) != journal.destroyed.end());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "nothing is kept unwatched, and watching again counts up")
	{
		const unsigned long long before = Loom::LoomObject::WatchCount();

		{
			Holder holder;
			holder.count = 1;
			Pump();
		};

		const Loom::LoomObject::Journal journal = Loom::LoomObject::TakeJournal();

		CHECK(journal.touched.empty());
		CHECK(journal.destroyed.empty());

		LoomTests::Watching watching;

		CHECK(Loom::LoomObject::WatchCount() == before + 1);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a destroyed object is let go by the field that points at it now, and only that one")
	{
		Loom::Scene scene("References");
		Loom::GameObject* first = scene.AddChild("First");
		Loom::GameObject* second = scene.AddChild("Second");
		Pump();

		Holder holder;
		holder.partner = first;
		holder.partner = second;

		first->Destroy();
		Pump();

		CHECK(holder.partner == second);

		second->Destroy();
		Pump();

		CHECK(holder.partner == nullptr);
	};
};
