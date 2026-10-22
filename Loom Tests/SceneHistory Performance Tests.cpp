#include "doctest.h"

#include "Test Support.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Scene.h"
#include "SceneHistory.h"

#include "glm/glm.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using LoomTests::Pump;


namespace
{
	// Objects sit in groups of this many under the scene's root, as a scene
	// built by hand tends to be laid out.
	constexpr int GROUP = 16;

	// Each pass makes this many edits, undoes each and redoes each, so a
	// pass is long enough to time.
	constexpr int EDITS = 50;

	// A pass is timed this many times and the quickest kept, so a thread
	// switched out mid-pass costs a retry rather than the result.
	constexpr int TRIES = 5;

	constexpr double MICROSECONDS_PER_MILLISECOND = 1000.0;

	// What an object in a real scene carries: a few fields, one of them a
	// reference to another object.
	struct Load final : Loom::Component<Load>
	{
		Loom::Serial<std::string> label = "unset";
		Loom::Serial<float> weight = 1.0f;
		Loom::Serial<glm::vec3> offset{ };
		Loom::Serial<Loom::GameObject*> partner;
	};

	struct LoadRegistered
	{
		LoadRegistered()  { Loom::ComponentRegistry::Register<Load>("Load"); };
		~LoadRegistered() { Loom::ComponentRegistry::Unregister("Load"); };
	};

	// The quickest of TRIES passes, per edit, undo and redo, in milliseconds.
	struct Costs
	{
		double edit = std::numeric_limits<double>::max();
		double undo = std::numeric_limits<double>::max();
		double redo = std::numeric_limits<double>::max();
	};

	// A scene of count objects, each with a Load pointing at the one before.
	struct Crowd
	{
		explicit Crowd(int count) :
			scene(new Loom::Scene("Crowd"))
		{
			Pump();

			Loom::GameObject* group = nullptr;

			for (int i = 0; i < count; i++)
			{
				if (i % GROUP == 0)
					group = scene->AddChild("Group");

				Loom::GameObject* object = group->AddChild("Object");
				Load* load = object->Attach<Load>();

				load->label = "Object " + std::to_string(i);

				if (!objects.empty())
					load->partner = objects.back();

				objects.push_back(object);
			};

			Pump();
		};

		~Crowd()
		{
			delete scene;
			Pump();
		};

		Costs Time()
		{
			using Clock = std::chrono::steady_clock;

			const auto since =
				[](Clock::time_point start)
				{
					return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
				};

			const std::vector<Loom::Scene*> open{ scene };
			Loom::SceneHistory history{ };
			Costs best{ };

			history.Record(open);

			for (int pass = 0; pass < TRIES; pass++)
			{
				// Spread through the scene, so no part of it stays warm.
				const size_t stride = objects.size() / EDITS;

				Clock::time_point start = Clock::now();

				for (int i = 0; i < EDITS; i++)
				{
					objects[i * stride]->GetComponent<Load>()->label = "Pass " + std::to_string(pass);
					history.Record(open);
				};

				best.edit = std::min(best.edit, since(start) / EDITS);
				start = Clock::now();

				for (int i = 0; i < EDITS; i++)
					history.Undo(open);

				best.undo = std::min(best.undo, since(start) / EDITS);
				start = Clock::now();

				for (int i = 0; i < EDITS; i++)
					history.Redo(open);

				best.redo = std::min(best.redo, since(start) / EDITS);
			};

			return best;
		};

		Loom::Scene* scene = nullptr;
		std::vector<Loom::GameObject*> objects{ };
	};
};


TEST_SUITE("Performance" * doctest::skip())
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an edit, an undo and a redo cost about the same among sixteen times the GameObjects")
	{
		constexpr int FEW = 4000;
		constexpr int GROWTH = 16;
		constexpr int MANY = FEW * GROWTH;

		// Looking at every object would make each sixteen times the cost;
		// this leaves room for larger tables missing the cache more.
		constexpr double MOST_GROWTH = 3.0;

		LoadRegistered registered;
		LoomTests::Watching watching;

		Costs few{ };
		Costs many{ };

		{
			Crowd crowd(FEW);
			few = crowd.Time();
		};

		{
			Crowd crowd(MANY);
			many = crowd.Time();
		};

		std::ostringstream report;
		report << "among " << FEW << " GameObjects then " << MANY << ", in us: edit "
			<< few.edit * MICROSECONDS_PER_MILLISECOND << " then " << many.edit * MICROSECONDS_PER_MILLISECOND << ", undo "
			<< few.undo * MICROSECONDS_PER_MILLISECOND << " then " << many.undo * MICROSECONDS_PER_MILLISECOND << ", redo "
			<< few.redo * MICROSECONDS_PER_MILLISECOND << " then " << many.redo * MICROSECONDS_PER_MILLISECOND;
		MESSAGE(report.str());

		CHECK(many.edit / few.edit <= MOST_GROWTH);
		CHECK(many.undo / few.undo <= MOST_GROWTH);
		CHECK(many.redo / few.redo <= MOST_GROWTH);
	};
};
