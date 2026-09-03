#pragma once

// Shared scaffolding for the Loom test suite.
//
// Everything here is header-only so both builds (MSVC and emcc) pick it up the
// same way, and so a test file only has to include this one header to get at
// the engine's deferred-work pump and the probe components.

#include "Engine.h"
#include "Component.h"

#include <string>
#include <vector>


namespace LoomTests
{
	// GameObject, Scene and LoomObject all defer their real work through
	// Engine::QueueTask, which the engine normally drains once a frame. Tests
	// have no frame loop, so they call this instead. Nothing an engine object
	// promises is observable until it has run.
	inline void Pump()
	{
		Loom::Engine::DoTasks();
	};

	// Base for test cases that touch engine objects. Draining on the way in
	// discards anything a previous case left queued; draining on the way out
	// keeps this case's leftovers from firing inside the next one, where they
	// would be running against objects that have already been destroyed.
	struct EngineFixture
	{
		EngineFixture()  { Pump(); };
		~EngineFixture() { Pump(); };
	};


	// A component that records every lifecycle callback the engine makes on it,
	// so tests can assert on what ran and in what order.
	//
	// TAG lets a test have several distinct component types (Attach and
	// GetComponent key off the concrete type) without writing each one out.
	template <char TAG = 'P'>
	struct Probe final : Loom::Component<Probe<TAG>>
	{
		void OnAttach()  override { calls.emplace_back("attach");  };
		void OnDetach()  override { calls.emplace_back("detach");  };
		void OnUpdate()  override { calls.emplace_back("update");  };
		void OnRender()  override { calls.emplace_back("render");  };
		void OnPhysics() override { calls.emplace_back("physics"); };

		size_t Count(const std::string& call) const
		{
			size_t n = 0;
			for (const std::string& made : calls)
				if (made == call)
					n++;
			return n;
		};

		std::vector<std::string> calls{ };
	};

	// A component that overrides nothing. GameObject::Attach inspects each
	// callback at compile time and only registers the component on the lists it
	// actually implements, so this one should never end up on any of them.
	struct InertComponent final : Loom::Component<InertComponent>
	{ };

	// Overrides OnUpdate only, to pin down that update and render registration
	// are decided independently.
	struct UpdateOnlyComponent final : Loom::Component<UpdateOnlyComponent>
	{
		void OnUpdate() override { updates++; };

		int updates = 0;
	};

	// Takes constructor arguments, to cover Attach's argument forwarding.
	struct ConstructedComponent final : Loom::Component<ConstructedComponent>
	{
		ConstructedComponent(int a, std::string b) :
			number(a),
			text(std::move(b))
		{ };

		int number;
		std::string text;
	};
};
