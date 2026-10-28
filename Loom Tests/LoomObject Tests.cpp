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

#pragma region Helpers

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

	// Whether the journal names object for what it holds, or with placed for
	// where it sits.
	bool Names(const Loom::LoomObject::Journal& journal, const Loom::LoomObject* object, bool placed = false)
	{
		return std::any_of(journal.touched.begin(), journal.touched.end(),
			[&](const auto& touched) { return touched.object == object && (placed ? touched.placed : touched.own); });
	};
};

#pragma endregion


TEST_SUITE("LoomObject")
{
	#pragma region Names and IDs

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

	// Named before the queue drains, as every GameObject and Scene is in its
	// constructor, and found both before and after it drains.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "GetByID finds a registered object")
	{
		Thing thing;
		thing.SetName("Findable");

		CHECK(Loom::LoomObject::GetByID(thing.m_ID) == &thing);

		Pump();

		CHECK(Loom::LoomObject::GetByID(thing.m_ID) == &thing);

		Pump();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "GetByName finds an object by its name, before and after the queue drains")
	{
		Thing thing;
		thing.SetName("Player");

		CHECK(Loom::LoomObject::GetByName("Player") == &thing);

		Pump();

		CHECK(Loom::LoomObject::GetByName("Player") == &thing);
		CHECK(Loom::LoomObject::GetByName("Nobody") == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "renaming an object lets its old name go")
	{
		Thing thing;
		thing.SetName("Before");
		Pump();

		thing.SetName("After");

		CHECK(Loom::LoomObject::GetByName("Before") == nullptr);
		CHECK(Loom::LoomObject::GetByName("After") == &thing);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an object destroyed before the queue drains leaves nothing found under its name")
	{
		{
			Thing brief;
			brief.SetName("Brief");
		};

		Pump();

		CHECK(Loom::LoomObject::GetByName("Brief") == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "destroying one of two objects with a name leaves the other found by it")
	{
		Thing kept;
		kept.SetName("Twin");

		{
			Thing gone;
			gone.SetName("Twin");
			Pump();
		};

		CHECK(Loom::LoomObject::GetByName("Twin") == &kept);

		{
			Thing gone;
			Pump();

			gone.SetName("Twin");
		};

		CHECK(Loom::LoomObject::GetByName("Twin") == &kept);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "GetByID returns null for an ID that was never issued")
	{
		CHECK(Loom::LoomObject::GetByID(~Loom::uint64_t(0)) == nullptr);

		Pump();
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "destroying an object unregisters its ID")
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

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "GetByID casts to the requested type")
	{
		Thing thing;
		thing.SetName("Typed");
		Pump();

		const Thing* found = Loom::LoomObject::GetByID<Thing>(thing.m_ID);

		CHECK(found == &thing);

		Pump();
	};

	#pragma endregion

	#pragma region Journal

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
			[&](const auto& touched) { return touched.object == &holder; }) == 1);
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

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a child coming, going or moving puts it and the sibling before it in the journal, and nothing else")
	{
		LoomTests::Watching watching;

		Loom::Scene scene("Journal");
		Loom::GameObject* parent = scene.AddChild("Parent");
		Loom::GameObject* first = parent->AddChild("First");
		Loom::GameObject* second = parent->AddChild("Second");
		Pump();
		Loom::LoomObject::TakeJournal();

		Loom::GameObject* third = parent->AddChild("Third");
		Pump();

		Loom::LoomObject::Journal journal = Loom::LoomObject::TakeJournal();

		CHECK(Names(journal, third, true));
		CHECK(Names(journal, second, true));
		CHECK_FALSE(Names(journal, first, true));
		CHECK_FALSE(Names(journal, parent));
		CHECK_FALSE(Names(journal, parent, true));

		first->SetParent(parent, third);
		Pump();
		journal = Loom::LoomObject::TakeJournal();

		CHECK(parent->GetChildren() == std::vector<Loom::GameObject*>{ second, first, third });
		CHECK(first->GetSiblingIndex() == 1);
		CHECK(Names(journal, first, true));
		CHECK(Names(journal, second, true));
		CHECK_FALSE(Names(journal, third, true));
		CHECK_FALSE(Names(journal, first));

		first->Destroy();
		Pump();
		journal = Loom::LoomObject::TakeJournal();

		CHECK(Names(journal, second, true));
		CHECK_FALSE(Names(journal, third, true));
		CHECK_FALSE(Names(journal, parent, true));
		CHECK(third->GetSiblingIndex() == 1);
		CHECK(std::find(journal.destroyed.begin(), journal.destroyed.end(), first) != journal.destroyed.end());
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

	#pragma endregion
};
