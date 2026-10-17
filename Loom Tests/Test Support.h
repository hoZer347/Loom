#pragma once

// Shared scaffolding for the Loom test suite.
//
// Everything here is header-only so both builds (MSVC and emcc) pick it up the
// same way, and so a test file only has to include this one header to get at
// the engine's deferred-work pump and the probe components.

#include "Engine.h"
#include "Component.h"
#include "Renderer.h"

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
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


	// Stands in for the Engine's renderer while it lives, which a test has no
	// window to open. It hands out handles, records what it is given, and
	// compiles anything but a fragment stage that says BROKEN. The window is
	// windowSize, and a colour target the size it was made.
	struct RecordingRenderer final : Loom::Renderer
	{
		static constexpr const char* BROKEN = "BROKEN";
		static constexpr int MAX_TEXTURE_SIZE = 4096;
		static constexpr uint32_t WINDOW = 0;

		struct Uniform
		{
			Loom::UniformType type;
			std::array<uint32_t, Loom::MAX_UNIFORM_COMPONENTS> components{ };

			float Float(int i) const { return std::bit_cast<float>(components[i]); };
			int32_t Int(int i) const { return (int32_t)components[i]; };
		};

		RecordingRenderer() : m_previous(current) { current = this; };
		~RecordingRenderer() { current = m_previous; };

		Loom::Backend GetBackend() const override { return Loom::Backend::OpenGL; };
		bool Attach(GLFWwindow*) override { return true; };

		void InitImGui(GLFWwindow*) override { };
		void ShutdownImGui() override { };
		void NewImGuiFrame() override { };
		void RenderImGui(ImDrawData*) override { };
		void RenderImGuiWindows() override { };
		void BeginFrame() override { };
		void EndFrame() override { };
		void Capture(const CaptureHandler&) override { };

		uint32_t CreateProgram(const std::string& name, const std::string&, const std::string& fragment) override
		{
			if (fragment.find(BROKEN) != std::string::npos)
				throw std::runtime_error("Shader compile error in " + name);

			return programs[name] = ++m_handles;
		};

		void DeleteProgram(uint32_t program) override { deleted.push_back(program); };

		void SetUniform(uint32_t, const char* name, float value) override { floats[name] = value; };
		void SetUniform(uint32_t, const char*, const glm::vec3&) override { };
		void SetUniform(uint32_t, const char*, const glm::vec4&) override { };
		void SetUniform(uint32_t, const char* name, const glm::mat4& value) override { matrices[name] = value; };

		void SetUniform(uint32_t, const char* name, Loom::UniformType type, const void* components) override
		{
			const Loom::UniformTypeInfo& info = Loom::InfoOf(type);

			Uniform& uniform = uniforms[name];
			uniform.type = type;
			std::copy_n((const uint32_t*)components, info.columns * info.rows, uniform.components.begin());
		};

		void SetTexture(uint32_t, const char* name, uint32_t texture) override { textures[name] = texture; };

		void Draw(uint32_t program, uint32_t, const float* vertices, size_t count, uint32_t, Loom::VertexFormat format) override
		{
			drawn.push_back(program);
			drawnInto.push_back(m_targets.empty() ? WINDOW : m_targets.back());
			lastVertices.assign(vertices, vertices + count * Loom::FloatsPerVertex(format));
			lastFormat = format;
		};

		void SetPixelated(uint32_t program, bool pixelated) override
		{
			if (pixelated)
				pixelatedPrograms.insert(program);
			else pixelatedPrograms.erase(program);
		};

		uint32_t CreateTexture(int, int, const uint8_t*) override { return ++m_handles; };
		uint32_t CreateDepthTarget(int) override { return ++m_handles; };
		uint32_t CreateColorTarget(int width, int height) override
		{
			++m_handles;
			targetSizes[m_handles] = glm::ivec2(width, height);
			return m_handles;
		};
		void DestroyTexture(uint32_t) override { };

		void PushTarget(uint32_t target) override { m_targets.push_back(target); };
		void PopTarget() override { m_targets.pop_back(); };
		void Clear(const float*, bool) override { };
		void SetDepthBias(float, float) override { };
		void SetColorWrite(bool write) override { colorWrites.push_back(write); };
		void SetDepthTestEqual(bool equal) override { depthTestsEqual.push_back(equal); };

		glm::ivec2 TargetSize() const override
		{
			return m_targets.empty() ? windowSize : targetSizes.at(m_targets.back());
		};
		int MaxTextureSize() const override { return MAX_TEXTURE_SIZE; };
		void* ImGuiTexture(uint32_t) override { return nullptr; };

		std::map<std::string, float> floats;
		std::map<std::string, glm::mat4> matrices;
		std::map<std::string, Uniform> uniforms;
		std::map<std::string, uint32_t> textures;
		std::vector<uint32_t> deleted;
		std::vector<uint32_t> drawn;
		std::vector<bool> colorWrites;
		std::vector<bool> depthTestsEqual;

		// What each of drawn was drawn into: a target, or WINDOW.
		std::vector<uint32_t> drawnInto;

		// The vertices of the last draw, as handed over, and how they were laid out.
		std::vector<float> lastVertices;
		Loom::VertexFormat lastFormat = Loom::VertexFormat::Position;

		std::set<uint32_t> pixelatedPrograms;

		std::map<std::string, uint32_t> programs;
		std::map<uint32_t, glm::ivec2> targetSizes;
		glm::ivec2 windowSize = glm::ivec2(1);

	private:
		Renderer* m_previous;
		uint32_t m_handles = 0;
		std::vector<uint32_t> m_targets;
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
