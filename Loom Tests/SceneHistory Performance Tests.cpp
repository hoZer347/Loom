#include "doctest.h"

#include "Test Support.h"

#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Guid.h"
#include "Scene.h"
#include "SceneHistory.h"

#include "glm/glm.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using LoomTests::Pump;


namespace
{
	// Objects sit in groups of this many under the scene's root, as a scene
	// built by hand tends to be laid out, or all under the root.
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

	struct Crowd;

	// The edits timed: the edit made to the object numbered by the int.
	struct Kind
	{
		const char* name;
		std::function<void(Crowd&, int)> edit;
	};

	// A scene of count objects, each with a Load pointing at the one before,
	// in groups or all under the root.
	struct Crowd
	{
		Crowd(int count, bool grouped) :
			scene(new Loom::Scene("Crowd"))
		{
			Pump();

			Loom::GameObject* group = &scene->GetRoot();
			Loom::GameObject* last = nullptr;

			for (int i = 0; i < count; i++)
			{
				if (grouped && i % GROUP == 0)
					group = scene->AddChild("Group");

				Loom::GameObject* object = group->AddChild("Object");
				Load* load = object->Attach<Load>();

				load->label = "Object " + std::to_string(i);
				load->partner = last;

				guids.push_back(object->GetGuid());
				last = object;
			};

			Pump();
		};

		~Crowd()
		{
			delete scene;
			Pump();
		};

		// By guid, since an undone delete makes the object again.
		Loom::GameObject* At(int i) const
		{
			return Loom::LoomObject::GetByGuid<Loom::GameObject>(guids[i]);
		};

		Costs Time(const Kind& kind)
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

			// Spread through the scene, so no part of it stays warm.
			const int stride = (int)guids.size() / EDITS;

			for (int pass = 0; pass < TRIES; pass++)
			{
				Clock::time_point start = Clock::now();

				for (int i = 0; i < EDITS; i++)
				{
					kind.edit(*this, i * stride);
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

				// Back as it was for the next pass.
				for (int i = 0; i < EDITS; i++)
					history.Undo(open);
			};

			return best;
		};

		Loom::Scene* scene = nullptr;
		std::vector<Loom::Guid> guids{ };
	};

	const std::vector<Kind> KINDS =
	{
		{
			"field",
			[](Crowd& crowd, int i)
			{
				crowd.At(i)->GetComponent<Load>()->label = "Edited";
			}
		},
		{
			"duplicate",
			[](Crowd& crowd, int i)
			{
				Loom::GameObject* original = crowd.At(i);
				Loom::GameObject* copy = original->GetParent()->AddChild("Copy");

				copy->Attach<Load>()->partner = original;
				copy->SetParent(original->GetParent(), original);
			}
		},
		{
			"delete",
			[](Crowd& crowd, int i)
			{
				crowd.At(i)->Destroy();
			}
		},
		{
			"move",
			[](Crowd& crowd, int i)
			{
				Loom::GameObject* across = crowd.At((i + (int)crowd.guids.size() / 2) % (int)crowd.guids.size());

				crowd.At(i)->SetParent(across->GetParent(), across);
			}
		},
	};
};


TEST_SUITE("Performance" * doctest::skip())
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an edit, an undo and a redo cost about the same among sixteen times the GameObjects")
	{
		constexpr int few = 4000;
		constexpr int growth = 16;
		constexpr int many = few * growth;

		// Looking at every object would make each sixteen times the cost;
		// this leaves room for larger tables missing the cache more.
		constexpr double most_growth = 3.0;

		LoadRegistered registered;
		LoomTests::Watching watching;

		for (const bool grouped : { true, false })
			for (const Kind& kind : KINDS)
			{
				Costs fewCosts{ };
				Costs manyCosts{ };

				{
					Crowd crowd(few, grouped);
					fewCosts = crowd.Time(kind);
				};

				{
					Crowd crowd(many, grouped);
					manyCosts = crowd.Time(kind);
				};

				std::ostringstream report;
				report << kind.name << (grouped ? " in groups" : " all under the root") << ", among " << few << " GameObjects then " << many << ", in us: edit "
					<< fewCosts.edit * MICROSECONDS_PER_MILLISECOND << " then " << manyCosts.edit * MICROSECONDS_PER_MILLISECOND << ", undo "
					<< fewCosts.undo * MICROSECONDS_PER_MILLISECOND << " then " << manyCosts.undo * MICROSECONDS_PER_MILLISECOND << ", redo "
					<< fewCosts.redo * MICROSECONDS_PER_MILLISECOND << " then " << manyCosts.redo * MICROSECONDS_PER_MILLISECOND;
				MESSAGE(report.str());

				CHECK(manyCosts.edit / fewCosts.edit <= most_growth);
				CHECK(manyCosts.undo / fewCosts.undo <= most_growth);
				CHECK(manyCosts.redo / fewCosts.redo <= most_growth);
			};
	};
};
