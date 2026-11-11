#include "doctest.h"

#include "Test Support.h"

#include "GameObject.h"
#include "Hierarchy.h"
#include "Scene.h"

#include <chrono>
#include <utility>
#include <vector>

using LoomTests::Pump;
using Loom::Hierarchy::Placement;
using Loom::Hierarchy::Step;
using Loom::Hierarchy::View;
using Loom::Hierarchy::Zone;


#pragma region Helpers

namespace
{
	constexpr float ROW = 10.0f;

	// What a plan draws, as the rows stepped over before each child and the
	// child, with no child after the last.
	using Drawn = std::vector<std::pair<size_t, const Loom::GameObject*>>;

	Drawn DrawnBy(const std::vector<Step>& steps)
	{
		Drawn drawn{ };

		for (const Step& step : steps)
			drawn.emplace_back(step.skip, step.child);

		return drawn;
	};

	// count children under parent, made and settled.
	std::vector<Loom::GameObject*> Children(Loom::GameObject& parent, size_t count)
	{
		for (size_t i = 0; i < count; i++)
			parent.AddChild();

		Pump();

		return parent.GetChildren();
	};

	// Applies a drop the way the Editor does, one object after another.
	void Drop(const std::vector<Loom::GameObject*>& moving, const Placement& placement)
	{
		for (Loom::GameObject* gameObject : moving)
			gameObject->SetParent(placement.parent, placement.before);

		Pump();
	};
};

#pragma endregion


TEST_SUITE("Hierarchy")
{
	#pragma region Visible Rows

	TEST_CASE("Visible is the rows the view shows, at the top, the middle and the bottom of the list")
	{
		constexpr size_t count = 100;

		CHECK(Loom::Hierarchy::Visible(View{ 0.0f, ROW, 0.0f, 35.0f }, count) == std::pair<size_t, size_t>{ 0, 4 });
		CHECK(Loom::Hierarchy::Visible(View{ -500.0f, ROW, 0.0f, 35.0f }, count) == std::pair<size_t, size_t>{ 50, 54 });
		CHECK(Loom::Hierarchy::Visible(View{ -950.0f, ROW, 0.0f, 35.0f }, count) == std::pair<size_t, size_t>{ 95, 99 });
		CHECK(Loom::Hierarchy::Visible(View{ -950.0f, ROW, 0.0f, 100.0f }, count) == std::pair<size_t, size_t>{ 95, 100 });
	};

	TEST_CASE("Visible is empty for no rows, and for a view wholly above or below the list")
	{
		constexpr size_t count = 100;

		const auto [noneFirst, noneLast] = Loom::Hierarchy::Visible(View{ 0.0f, ROW, 0.0f, 35.0f }, 0);
		const auto [aboveFirst, aboveLast] = Loom::Hierarchy::Visible(View{ 500.0f, ROW, 0.0f, 35.0f }, count);
		const auto [belowFirst, belowLast] = Loom::Hierarchy::Visible(View{ -5000.0f, ROW, 0.0f, 35.0f }, count);

		CHECK(noneFirst == noneLast);
		CHECK(aboveFirst == aboveLast);
		CHECK(belowFirst == belowLast);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Contains is true for the root and what is under it, and false for anything else")
	{
		Loom::Scene scene("Contains");
		Loom::GameObject* parent = scene.AddChild("Parent");
		Loom::GameObject* child = parent->AddChild("Child");
		Loom::GameObject* other = scene.AddChild("Other");
		Pump();

		CHECK(Loom::Hierarchy::Contains(*parent, parent));
		CHECK(Loom::Hierarchy::Contains(*parent, child));
		CHECK(Loom::Hierarchy::Contains(scene.GetRoot(), child));
		CHECK_FALSE(Loom::Hierarchy::Contains(*parent, other));
		CHECK_FALSE(Loom::Hierarchy::Contains(*child, parent));
		CHECK_FALSE(Loom::Hierarchy::Contains(*parent, nullptr));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "CountRowsBelow counts open descendants in, an open leaf as none, and sorts by sibling index")
	{
		Loom::Scene scene("Rows");
		Loom::GameObject* group = scene.AddChild("Group");
		Loom::GameObject* closed = scene.AddChild("Closed");
		Pump();

		const std::vector<Loom::GameObject*> inGroup = Children(*group, 3);
		const std::vector<Loom::GameObject*> inMiddle = Children(*inGroup[1], 2);
		const std::vector<Loom::GameObject*> inClosed = Children(*closed, 1);
		Children(*inClosed[0], 4);

		Loom::Hierarchy::OpenUnder openUnder
		{
			{ &scene.GetRoot(), { { 0, group } } },
			{ group, { { 1, inGroup[1] }, { 0, inGroup[0] } } },
			{ closed, { { 0, inClosed[0] } } },
		};

		const Loom::Hierarchy::RowsBelow rowsBelow = Loom::Hierarchy::CountRowsBelow(openUnder);

		CHECK(rowsBelow.at(inGroup[1]) == 2);
		CHECK(rowsBelow.at(inGroup[0]) == 0);
		CHECK(rowsBelow.at(group) == 5);
		CHECK(rowsBelow.at(inClosed[0]) == 4);
		CHECK_FALSE(rowsBelow.contains(closed));
		CHECK(openUnder.at(group).front().second == inGroup[0]);
	};

	#pragma endregion

	#pragma region Plans

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Plan draws the closed rows in view and steps over the rest")
	{
		Loom::Scene scene("Plan");
		const std::vector<Loom::GameObject*> children = Children(scene.GetRoot(), 10);

		const std::vector<Step> steps = Loom::Hierarchy::Plan(scene.GetRoot(), nullptr, { }, View{ 0.0f, ROW, 25.0f, 55.0f }, nullptr);

		CHECK(DrawnBy(steps) == Drawn{ { 2, children[2] }, { 0, children[3] }, { 0, children[4] }, { 0, children[5] }, { 4, nullptr } });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Plan steps over an open child out of view by all its rows, and draws one partly in view whole")
	{
		Loom::Scene scene("Plan");
		const std::vector<Loom::GameObject*> children = Children(scene.GetRoot(), 5);
		Children(*children[0], 3);

		Loom::Hierarchy::OpenUnder openUnder{ { &scene.GetRoot(), { { 0, children[0] } } } };
		const Loom::Hierarchy::RowsBelow rowsBelow = Loom::Hierarchy::CountRowsBelow(openUnder);
		const Loom::Hierarchy::OpenChildren* open = &openUnder.at(&scene.GetRoot());

		const std::vector<Step> below = Loom::Hierarchy::Plan(scene.GetRoot(), open, rowsBelow, View{ 0.0f, ROW, 50.0f, 70.0f }, nullptr);
		const std::vector<Step> across = Loom::Hierarchy::Plan(scene.GetRoot(), open, rowsBelow, View{ 0.0f, ROW, 30.0f, 50.0f }, nullptr);

		CHECK(DrawnBy(below) == Drawn{ { 5, children[2] }, { 0, children[3] }, { 1, nullptr } });
		CHECK(DrawnBy(across) == Drawn{ { 0, children[0] }, { 0, children[1] }, { 3, nullptr } });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Plan draws the kept row wherever it is, and the open child it is under")
	{
		Loom::Scene scene("Plan");
		const std::vector<Loom::GameObject*> children = Children(scene.GetRoot(), 100);
		const std::vector<Loom::GameObject*> grandchildren = Children(*children[0], 3);

		const View top{ 0.0f, ROW, 0.0f, 35.0f };
		const View middle{ -500.0f, ROW, 0.0f, 35.0f };

		Loom::Hierarchy::OpenUnder openUnder{ { &scene.GetRoot(), { { 0, children[0] } } } };
		const Loom::Hierarchy::RowsBelow rowsBelow = Loom::Hierarchy::CountRowsBelow(openUnder);

		const std::vector<Step> below = Loom::Hierarchy::Plan(scene.GetRoot(), nullptr, { }, top, children[50]);
		const std::vector<Step> above = Loom::Hierarchy::Plan(scene.GetRoot(), nullptr, { }, middle, children[10]);
		const std::vector<Step> under = Loom::Hierarchy::Plan(scene.GetRoot(), &openUnder.at(&scene.GetRoot()), rowsBelow, middle, grandchildren[1]);

		CHECK(DrawnBy(below) == Drawn{ { 0, children[0] }, { 0, children[1] }, { 0, children[2] }, { 0, children[3] }, { 46, children[50] }, { 49, nullptr } });
		CHECK(DrawnBy(above) == Drawn{ { 10, children[10] }, { 39, children[50] }, { 0, children[51] }, { 0, children[52] }, { 0, children[53] }, { 46, nullptr } });
		CHECK(DrawnBy(under).front() == std::pair<size_t, const Loom::GameObject*>{ 0, children[0] });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Plan takes an open child moved since it was gathered as a closed row")
	{
		Loom::Scene scene("Plan");
		const std::vector<Loom::GameObject*> children = Children(scene.GetRoot(), 4);
		Children(*children[3], 2);

		Loom::Hierarchy::OpenUnder openUnder{ { &scene.GetRoot(), { { 0, children[3] } } } };
		const Loom::Hierarchy::RowsBelow rowsBelow = Loom::Hierarchy::CountRowsBelow(openUnder);

		const std::vector<Step> steps = Loom::Hierarchy::Plan(scene.GetRoot(), &openUnder.at(&scene.GetRoot()), rowsBelow, View{ 0.0f, ROW, 0.0f, 100.0f }, nullptr);

		CHECK(DrawnBy(steps) == Drawn{ { 0, children[0] }, { 0, children[1] }, { 0, children[2] }, { 0, children[3] }, { 0, nullptr } });
	};

	#pragma endregion

	#pragma region Placing

	TEST_CASE("ZoneOf takes a quarter of the row at each end beside it, and all of a root's row into it")
	{
		CHECK(Loom::Hierarchy::ZoneOf(0.0f, ROW, 1.0f, false) == Zone::Above);
		CHECK(Loom::Hierarchy::ZoneOf(0.0f, ROW, 2.5f, false) == Zone::Above);
		CHECK(Loom::Hierarchy::ZoneOf(0.0f, ROW, 5.0f, false) == Zone::Into);
		CHECK(Loom::Hierarchy::ZoneOf(0.0f, ROW, 7.5f, false) == Zone::Below);
		CHECK(Loom::Hierarchy::ZoneOf(0.0f, ROW, 9.0f, false) == Zone::Below);
		CHECK(Loom::Hierarchy::ZoneOf(0.0f, ROW, 1.0f, true) == Zone::Into);
		CHECK(Loom::Hierarchy::ZoneOf(0.0f, ROW, 9.0f, true) == Zone::Into);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Place puts a drop before a row, last under it, or after it")
	{
		Loom::Scene scene("Place");
		const std::vector<Loom::GameObject*> children = Children(scene.GetRoot(), 3);
		const std::vector<Loom::GameObject*> grandchildren = Children(*children[1], 2);

		Loom::GameObject* root = &scene.GetRoot();

		CHECK(Loom::Hierarchy::Place(*children[1], false, Zone::Above, { }) == Placement{ root, children[1] });
		CHECK(Loom::Hierarchy::Place(*children[1], false, Zone::Into, { }) == Placement{ children[1], nullptr });
		CHECK(Loom::Hierarchy::Place(*children[0], false, Zone::Below, { }) == Placement{ root, children[1] });
		CHECK(Loom::Hierarchy::Place(*children[2], false, Zone::Below, { }) == Placement{ root, nullptr });
		CHECK(Loom::Hierarchy::Place(*root, true, Zone::Into, { }) == Placement{ root, nullptr });

		// Below an open row is above its first child, and below a closed one
		// is after the whole of it.
		CHECK(Loom::Hierarchy::Place(*children[1], true, Zone::Below, { }) == Placement{ children[1], grandchildren[0] });
		CHECK(Loom::Hierarchy::Place(*children[1], false, Zone::Below, { }) == Placement{ root, children[2] });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Place steps over the object being moved when it is next after the row")
	{
		Loom::Scene scene("Place");
		const std::vector<Loom::GameObject*> children = Children(scene.GetRoot(), 3);

		CHECK(Loom::Hierarchy::Place(*children[0], false, Zone::Below, { children[1] }) == Placement{ &scene.GetRoot(), children[2] });
		CHECK(Loom::Hierarchy::Place(*children[1], false, Zone::Below, { children[2] }) == Placement{ &scene.GetRoot(), nullptr });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Place steps over a run of moving siblings after the row, and to null when all the rest move")
	{
		Loom::Scene scene("Place");
		const std::vector<Loom::GameObject*> children = Children(scene.GetRoot(), 6);
		Loom::GameObject* root = &scene.GetRoot();

		CHECK(Loom::Hierarchy::Place(*children[0], false, Zone::Below, { children[1], children[2], children[3] }) == Placement{ root, children[4] });
		CHECK(Loom::Hierarchy::Place(*children[0], false, Zone::Below, { children[3], children[2], children[1] }) == Placement{ root, children[4] });
		CHECK(Loom::Hierarchy::Place(*children[0], false, Zone::Below, { children[1], children[3] }) == Placement{ root, children[2] });
		CHECK(Loom::Hierarchy::Place(*children[2], false, Zone::Below, { children[3], children[4], children[5] }) == Placement{ root, nullptr });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Moving is the dragged object alone when it is not selected")
	{
		Loom::Scene scene("Moving");
		const std::vector<Loom::GameObject*> children = Children(scene.GetRoot(), 4);

		CHECK(Loom::Hierarchy::Moving({ children[1], children[2] }, *children[3]) == std::vector<Loom::GameObject*>{ children[3] });
		CHECK(Loom::Hierarchy::Moving({ }, *children[0]) == std::vector<Loom::GameObject*>{ children[0] });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Moving drops roots and objects under another selected one")
	{
		Loom::Scene scene("Moving");
		const std::vector<Loom::GameObject*> children = Children(scene.GetRoot(), 3);
		const std::vector<Loom::GameObject*> grandchildren = Children(*children[0], 2);
		const std::vector<Loom::GameObject*> deep = Children(*grandchildren[0], 1);

		const std::vector<Loom::GameObject*> selection{ &scene.GetRoot(), children[0], grandchildren[0], deep[0], grandchildren[1], children[2] };

		CHECK(Loom::Hierarchy::Moving(selection, *children[2]) == std::vector<Loom::GameObject*>{ children[0], children[2] });
		CHECK(Loom::Hierarchy::Moving({ &scene.GetRoot(), children[1] }, *children[1]) == std::vector<Loom::GameObject*>{ children[1] });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Moving sorts a scattered selection across parents and depths into draw order")
	{
		Loom::Scene scene("Moving");
		const std::vector<Loom::GameObject*> children = Children(scene.GetRoot(), 4);
		const std::vector<Loom::GameObject*> in_first = Children(*children[0], 3);
		const std::vector<Loom::GameObject*> in_second = Children(*children[1], 2);
		const std::vector<Loom::GameObject*> deep = Children(*in_second[0], 2);

		const std::vector<Loom::GameObject*> selection
		{
			children[3],
			deep[1],
			in_first[2],
			children[1],
			in_first[0],
			deep[0],
		};

		// children[1] holds deep ones, so they go with it.
		CHECK(Loom::Hierarchy::Moving(selection, *children[3]) == std::vector<Loom::GameObject*>{ in_first[0], in_first[2], children[1], children[3] });

		const std::vector<Loom::GameObject*> split{ children[3], deep[1], in_first[2], in_second[1], in_first[0], deep[0] };

		CHECK(Loom::Hierarchy::Moving(split, *deep[0]) == std::vector<Loom::GameObject*>{ in_first[0], in_first[2], deep[0], deep[1], in_second[1], children[3] });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Dropping what Moving returns keeps its relative order, reordering up and down and reparenting")
	{
		Loom::Scene scene("Drop");
		const std::vector<Loom::GameObject*> children = Children(scene.GetRoot(), 7);
		const std::vector<Loom::GameObject*> target = Children(*children[6], 2);
		Loom::GameObject* root = &scene.GetRoot();

		// Up: 4 and 2 land above 1, in the order they were in.
		const std::vector<Loom::GameObject*> up = Loom::Hierarchy::Moving({ children[4], children[2] }, *children[4]);
		Drop(up, Loom::Hierarchy::Place(*children[0], false, Zone::Below, up));
		CHECK(root->GetChildren() == std::vector<Loom::GameObject*>{ children[0], children[2], children[4], children[1], children[3], children[5], children[6] });

		// Down: 0 and 2 land after 5, in the order they were in.
		const std::vector<Loom::GameObject*> down = Loom::Hierarchy::Moving({ children[2], children[0] }, *children[0]);
		Drop(down, Loom::Hierarchy::Place(*children[5], false, Zone::Below, down));
		CHECK(root->GetChildren() == std::vector<Loom::GameObject*>{ children[4], children[1], children[3], children[5], children[0], children[2], children[6] });

		// Reparent: 3 and 4 land under the last, after what it holds.
		const std::vector<Loom::GameObject*> into = Loom::Hierarchy::Moving({ children[3], children[4] }, *children[3]);
		Drop(into, Loom::Hierarchy::Place(*children[6], false, Zone::Into, into));
		CHECK(children[6]->GetChildren() == std::vector<Loom::GameObject*>{ target[0], target[1], children[4], children[3] });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "Moving a large deep selection takes a fraction of a frame's worth of time")
	{
		constexpr size_t sibling_count = 2000;
		constexpr double budget_milliseconds = 250.0;

		Loom::Scene scene("Large");
		Loom::GameObject* deepest = Children(*Children(*Children(scene.GetRoot(), 1)[0], 1)[0], 1)[0];
		const std::vector<Loom::GameObject*> selection = Children(*deepest, sibling_count);

		const auto start = std::chrono::steady_clock::now();
		const std::vector<Loom::GameObject*> moving = Loom::Hierarchy::Moving(selection, *selection.back());
		const double taken = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

		CHECK(moving == selection);
		CHECK(taken < budget_milliseconds);
	};

	#pragma endregion
};
