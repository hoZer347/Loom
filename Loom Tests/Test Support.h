#pragma once

// Shared scaffolding for the Loom test suite.
//
// Everything here is header-only so both builds (MSVC and emcc) pick it up the
// same way, and so a test file only has to include this one header to get at
// the engine's deferred-work pump and the probe components.

#include "Engine.h"
#include "Component.h"
#include "Renderer.h"

#include "glm/gtc/type_ptr.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>


namespace LoomTests
{
	// How far a computed float may drift from the value the math gives.
	constexpr float EPSILON = 1e-4f;

	// A closed mesh's vertices: a tetrahedron, each face wound counter-clockwise seen from outside.
	inline const std::vector<float> TETRAHEDRON = {
		0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
		1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f };

	#pragma region Pumping and Fixtures

	// GameObject, Scene and LoomObject all defer their real work through
	// Engine::QueueTask, which the engine normally drains once a frame. Tests
	// have no frame loop, so they call this instead. Nothing an engine object
	// promises is observable until it has run.
	inline void Pump()
	{
		Loom::Engine::DoTasks();
	};

	// A Skin's influences binding each vertex to one joint, all of its weight.
	inline std::vector<float> Bound(std::initializer_list<float> joints)
	{
		std::vector<float> influences;

		for (const float joint : joints)
			influences.insert(influences.end(), { joint, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f });

		return influences;
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

	// Keeps LoomObject's journal, which SceneHistory reads, for as long as it
	// lives.
	struct Watching
	{
		Watching()  { Loom::LoomObject::Watch(true);  };
		~Watching() { Loom::LoomObject::Watch(false); };
	};

	#pragma endregion


	#pragma region Probes

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

	#pragma endregion


	#pragma region Recording Renderer

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

		uint32_t CreateProgram(const std::string& name, const std::string& vertex, const std::string& fragment) override
		{
			if (fragment.find(BROKEN) != std::string::npos)
				throw std::runtime_error("Shader compile error in " + name);

			vertices[name] = vertex;
			fragments[name] = fragment;
			return programs[name] = ++m_handles;
		};

		void DeleteProgram(uint32_t program) override { deleted.push_back(program); };

		void SetUniform(uint32_t, const char* name, float value) override { floats[name] = value; };
		void SetUniform(uint32_t, const char*, const glm::vec3&) override { };
		void SetUniform(uint32_t, const char* name, const glm::vec4& value) override { vec4s[name] = value; };
		void SetUniform(uint32_t program, const char* name, const glm::mat4& value) override
		{
			matrices[name] = value;

			if (std::string_view(name) == "u_viewProjection")
				viewed.push_back(program);
		};

		void SetUniforms(uint32_t program, const char* name, const glm::mat4* values, size_t count) override
		{
			matrixArrays[name] = std::vector<glm::mat4>(values, values + count);
			matrixArrayPrograms[name] = program;
		};

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
			const Loom::VertexLayout layout = m_drawing ? *m_drawing : Loom::VertexLayout(format);

			if (recordVertices)
				lastVertices.assign(vertices, vertices + count * Loom::FloatsPerVertex(layout));

			lastFormat = format;
			lastLayout = layout;

			const auto color = vec4s.find("u_color");
			const auto texture = textures.find("u_texture");
			const auto scene = textures.find("u_scene");

			drawnColors.push_back(color == vec4s.end() ? glm::vec4(0.0f) : color->second);
			drawnTextures.push_back(texture == textures.end() ? 0 : texture->second);
			drawnScenes.push_back(scene == textures.end() ? 0 : scene->second);
			if (recordVertices)
				drawnVertices.push_back(lastVertices);
		};

		// Kept vertices are recorded as they are set, and drawn as Draw would draw them.
		uint32_t CreateVertices() override
		{
			kept[++m_handles] = { };
			return m_handles;
		};

		void SetVertices(uint32_t vertices, const float* data, size_t count, uint32_t, const Loom::VertexLayout& layout) override
		{
			kept[vertices] = { std::vector<float>(data, data + count * Loom::FloatsPerVertex(layout)), layout };
			handed++;
		};

		void DrawVertices(uint32_t program, uint32_t primitive, uint32_t vertices) override
		{
			const Kept& drawing = kept.at(vertices);

			m_drawing = &drawing.layout;
			Draw(program, primitive, drawing.vertices.data(), drawing.vertices.size() / Loom::FloatsPerVertex(drawing.layout), 0, drawing.layout.format);
			m_drawing = nullptr;
		};

		void DestroyVertices(uint32_t vertices) override { kept.erase(vertices); };

		// Each instance is recorded, and drawn as DrawVertices would draw it.
		void DrawInstances(uint32_t program, uint32_t primitive, uint32_t vertices, const float* instances, size_t count) override
		{
			constexpr size_t MODEL_FLOATS = sizeof(glm::mat4) / sizeof(float);

			instanceCounts.push_back(count);

			for (size_t i = 0; i < count; i++)
			{
				const float* instance = instances + i * FloatsPerInstance;

				instanceModels.push_back(glm::make_mat4(instance));
				instanceColors.push_back(glm::make_vec3(instance + MODEL_FLOATS));
				instanceSeeds.push_back(instance[FloatsPerInstance - 1]);
				DrawVertices(program, primitive, vertices);
			};
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
		uint32_t CreateDataTarget(int width, int height) override
		{
			dataTargetsAsked++;

			if (!dataTargets)
				return 0;

			++m_handles;
			targetSizes[m_handles] = glm::ivec2(width, height);
			return m_handles;
		};
		uint32_t TargetDepth(uint32_t target) override
		{
			const auto found = targetDepths.find(target);

			if (found != targetDepths.end())
				return found->second;

			return targetDepths[target] = ++m_handles;
		};
		void DestroyTexture(uint32_t texture) override { destroyedTextures.push_back(texture); };

		void PushTarget(uint32_t target) override { m_targets.push_back(target); };
		void PopTarget() override { m_targets.pop_back(); };
		void Clear(const float* color, bool depth) override { clears.push_back({ color != nullptr, depth }); };
		void SetDepthBias(float slope, float constant, float clamp) override { depthBiases.push_back({ slope, constant, clamp }); };
		void SetColorWrite(bool write) override { colorWrites.push_back(write); };
		void SetDepthTestEqual(bool equal) override { depthTestsEqual.push_back(equal); };
		void SetCullFace(Loom::CullFace cull) override { culls.push_back(cull); };

		glm::ivec2 TargetSize() const override
		{
			return m_targets.empty() ? windowSize : targetSizes.at(m_targets.back());
		};
		int MaxTextureSize() const override { return MAX_TEXTURE_SIZE; };
		void* ImGuiTexture(uint32_t) override { return nullptr; };

		std::map<std::string, float> floats;
		std::map<std::string, glm::mat4> matrices;
		std::map<std::string, std::vector<glm::mat4>> matrixArrays;
		std::map<std::string, uint32_t> matrixArrayPrograms;
		std::map<std::string, Uniform> uniforms;
		std::map<std::string, glm::vec4> vec4s;
		std::map<std::string, uint32_t> textures;
		std::vector<uint32_t> deleted;
		std::vector<uint32_t> destroyedTextures;
		std::vector<uint32_t> drawn;

		// Each program given u_viewProjection, as often as it was given it.
		std::vector<uint32_t> viewed;

		std::vector<bool> colorWrites;
		std::vector<bool> depthTestsEqual;
		std::vector<Loom::CullFace> culls;

		// Each depth bias set, as its slope, its constant and its clamp.
		std::vector<std::tuple<float, float, float>> depthBiases;

		// What each of drawn was drawn into: a target, or WINDOW.
		std::vector<uint32_t> drawnInto;

		// Whether draws record their vertices in lastVertices and drawnVertices,
		// which a timing test turns off so copying them does not count.
		bool recordVertices = true;

		// The vertices of the last draw, as handed over, and how they were laid out.
		std::vector<float> lastVertices;
		Loom::VertexFormat lastFormat = Loom::VertexFormat::Position;
		Loom::VertexLayout lastLayout;

		// For each of drawn: u_color, u_texture and u_scene as they were set, and its vertices.
		std::vector<glm::vec4> drawnColors;
		std::vector<uint32_t> drawnTextures;
		std::vector<uint32_t> drawnScenes;
		std::vector<std::vector<float>> drawnVertices;

		// How many instances each DrawInstances drew, and each instance's model
		// matrix, colour and seed.
		std::vector<size_t> instanceCounts;
		std::vector<glm::mat4> instanceModels;
		std::vector<glm::vec3> instanceColors;
		std::vector<float> instanceSeeds;

		// Each Clear: whether it was given a colour, and whether it cleared depth.
		std::vector<std::pair<bool, bool>> clears;

		std::set<uint32_t> pixelatedPrograms;

		struct Kept
		{
			std::vector<float> vertices;
			Loom::VertexLayout layout;
		};

		// What CreateVertices handed out and SetVertices filled, and how many
		// times SetVertices has been called.
		std::map<uint32_t, Kept> kept;
		size_t handed = 0;

		std::map<std::string, uint32_t> programs;
		std::map<std::string, std::string> vertices;
		std::map<std::string, std::string> fragments;
		std::map<uint32_t, glm::ivec2> targetSizes;

		// Whether CreateDataTarget makes one, as a GPU without float targets does not.
		bool dataTargets = true;
		int dataTargetsAsked = 0;
		std::map<uint32_t, uint32_t> targetDepths;
		glm::ivec2 windowSize = glm::ivec2(1);

	private:
		Renderer* m_previous;
		uint32_t m_handles = 0;
		std::vector<uint32_t> m_targets;

		// The layout of the kept vertices DrawVertices is drawing, if it is.
		const Loom::VertexLayout* m_drawing = nullptr;
	};

	#pragma endregion


	#pragma region Captures

	// Takes what is written to a stream for as long as it lives, so a test
	// that makes the engine complain on purpose keeps it off the console and
	// can check it was said. doctest reports on std::cout, so a capture of
	// std::cout ends before the checks that follow it.
	struct Captured final
	{
		explicit Captured(std::ostream& stream) :
			m_stream(stream),
			m_previous(stream.rdbuf(m_text.rdbuf()))
		{ };

		~Captured() { m_stream.rdbuf(m_previous); };

		Captured(const Captured&) = delete;
		Captured& operator=(const Captured&) = delete;

		std::string Text() const { return m_text.str(); };

		// How many times part was written.
		size_t Count(const std::string& part) const
		{
			if (part.empty())
				return 0;

			const std::string text = Text();
			size_t count = 0;

			for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size()))
				count++;

			return count;
		};

		bool Says(const std::string& part) const { return Count(part) > 0; };

	private:
		std::ostream& m_stream;
		std::ostringstream m_text;
		std::streambuf* m_previous;
	};

	#pragma endregion

	#pragma region Startup Clashes

	// What the ComponentRegistry suite's two Clash types say when they first
	// register, before main. main.cpp holds that line back from std::cerr and
	// counts it in StartupClashes; all else said then goes through.
	constexpr const char* STARTUP_CLASH = "Two component types are called Clash;";

	inline size_t& StartupClashes()
	{
		static size_t count = 0;
		return count;
	};

	#pragma endregion


	#pragma region Images

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

	#pragma endregion
};
