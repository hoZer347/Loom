#pragma once

// Shared scaffolding for the Loom test suite.
//
// Everything here is header-only so both builds (MSVC and emcc) pick it up the
// same way, and so a test file only has to include this one header to get at
// the engine's deferred-work pump and the probe components.

#include "Engine.h"
#include "Component.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
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


	// Writes width x height RGBA pixels, top row first, as an uncompressed
	// 32 bit TGA in the temp folder and answers its path. TGA because it is
	// short enough to write by hand and carries alpha.
	inline std::string WriteTga(const std::string& name, int width, int height, const std::vector<uint8_t>& rgba)
	{
		// Byte offsets in the header.
		enum { IMAGE_TYPE = 2, WIDTH = 12, HEIGHT = 14, DEPTH = 16, DESCRIPTOR = 17, HEADER_SIZE = 18 };

		constexpr uint8_t UNCOMPRESSED_TRUE_COLOR = 2;
		constexpr uint8_t BITS_PER_PIXEL = 32;
		constexpr uint8_t ALPHA_BITS = 8;
		constexpr uint8_t TOP_LEFT_ORIGIN = 0x20;
		constexpr int BYTE = 8;
		constexpr int RGBA = 4;

		uint8_t header[HEADER_SIZE]{ };
		header[IMAGE_TYPE] = UNCOMPRESSED_TRUE_COLOR;
		header[WIDTH] = (uint8_t)width;
		header[WIDTH + 1] = (uint8_t)(width >> BYTE);
		header[HEIGHT] = (uint8_t)height;
		header[HEIGHT + 1] = (uint8_t)(height >> BYTE);
		header[DEPTH] = BITS_PER_PIXEL;
		header[DESCRIPTOR] = ALPHA_BITS | TOP_LEFT_ORIGIN;

		const std::string path = (std::filesystem::temp_directory_path() / name).string();

		std::ofstream file(path, std::ios::binary);
		file.write((const char*)header, HEADER_SIZE);

		// TGA stores BGRA.
		for (size_t i = 0; i < rgba.size(); i += RGBA)
		{
			enum { R, G, B, A };
			const uint8_t bgra[RGBA] = { rgba[i + B], rgba[i + G], rgba[i + R], rgba[i + A] };
			file.write((const char*)bgra, RGBA);
		};

		return path;
	};
};
