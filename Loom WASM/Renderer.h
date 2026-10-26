#pragma once

#include "Loom API.h"

#include "UniformType.h"

#include "glm/glm.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct GLFWwindow;
struct ImDrawData;


namespace Loom
{
	enum class Backend { OpenGL, Vulkan };

	#pragma region Vertex Layout

	// What each vertex handed to Renderer::Draw holds, its floats one after
	// another: a position at location 0 (aPos), with PositionUV a texture
	// coordinate after it at location 1 (aUV), with PositionUVNormal a
	// normal after that at location 2 (aNormal), and with
	// PositionUVNormalColor a colour after that at location 3 (aColor, a
	// vec4). A shader reading aColor from vertices without one reads white.
	enum class VertexFormat { Position, PositionUV, PositionUVNormal, PositionUVNormalColor };

	LOOM_API size_t FloatsPerVertex(VertexFormat format);

	// A raw layer each vertex carries after its format's attributes: its id,
	// which a vertex stage reads it by as UV_<id>, and how many 32 bit words
	// of it each vertex holds.
	struct UVLayout
	{
		uint32_t id = 0;
		uint32_t words = 0;

		bool operator==(const UVLayout&) const = default;
	};

	// What each vertex handed to Renderer::SetVertices holds: its format's
	// floats, then each layer's words in turn, carried in the floats' bits.
	struct VertexLayout
	{
		VertexLayout(VertexFormat format = VertexFormat::Position) : format(format) { };

		VertexFormat format;
		std::vector<UVLayout> layers;

		bool operator==(const VertexLayout&) const = default;
	};

	LOOM_API size_t FloatsPerVertex(const VertexLayout& layout);

	// A vertex stage declaring UV_<id> reads it through attributes named
	// loom_UV_<id>_<slot>, four words a slot, which Shader::Assemble puts at
	// UVLayerLocation on. Parses one of those names, or nullopt for any other.
	struct UVSlot
	{
		uint32_t layer = 0;
		uint32_t slot = 0;
	};

	LOOM_API std::optional<UVSlot> ParseUVSlot(std::string_view attribute);

	// Where in each vertex, in floats, a slot's words start and how many of
	// its four the vertices hold, 0 when they hold none.
	struct UVSource
	{
		size_t offset = 0;
		uint32_t words = 0;
	};

	LOOM_API UVSource SourceOf(const VertexLayout& layout, UVSlot slot);

	#pragma endregion

	/**
	* Loom::Renderer
	* - The graphics API behind everything the engine draws, picked once when
	*   the Engine opens its window (Engine::backend). OpenGL is the only one on
	*   the web; desktop builds can also run on Vulkan
	* - Behaves like GL whichever is underneath: shaders are the same GLSL with
	*   loose uniforms, clip space is GL's, and a target reads back bottom row
	*   first. A scene, a script or a shader cannot tell which it is running on
	* - Programs and textures are plain handles, 0 meaning none. Under GL they
	*   are the GL names themselves, so raw GL code can keep using them
	*/
	struct LOOM_API Renderer
	{
		#pragma region Lifetime

		// Builds the renderer for a backend and sets the window hints it needs,
		// which is why it comes before the window does.
		static std::unique_ptr<Renderer> Create(Backend backend);

		// The renderer the Engine opened, or null before one has (a test with no
		// window).
		static Renderer* Get() { return current; };

		virtual ~Renderer() = default;

		virtual Backend GetBackend() const = 0;

		#pragma endregion

		#pragma region Window

		// Takes the window over. False when the API cannot run on it, which for
		// Vulkan means the Engine falls back to OpenGL.
		virtual bool Attach(GLFWwindow* window) = 0;

		#pragma endregion

		#pragma region ImGui

		virtual void InitImGui(GLFWwindow* window) = 0;
		virtual void ShutdownImGui() = 0;
		virtual void NewImGuiFrame() = 0;
		virtual void RenderImGui(ImDrawData* drawData) = 0;

		// The ImGui windows dragged out of the main one.
		virtual void RenderImGuiWindows() = 0;

		#pragma endregion

		#pragma region Frames

		// The pixels of a finished frame, red first and bottom row first.
		typedef std::function<void(int width, int height, const std::vector<uint8_t>& rgb)> CaptureHandler;

		// Starts a frame on the window, which is the target until something is
		// pushed over it, and presents it.
		virtual void BeginFrame() = 0;
		virtual void EndFrame() = 0;

		// Hands the window's pixels to the handler once everything drawn this
		// frame has landed, as the frame ends.
		virtual void Capture(const CaptureHandler& handler) = 0;

		#pragma endregion

		#pragma region Programs

		// Compiles the two stages, which carry no #version line. Throws
		// std::runtime_error with the compiler's log when either fails.
		virtual uint32_t CreateProgram(const std::string& name, const std::string& vertex, const std::string& fragment) = 0;
		virtual void DeleteProgram(uint32_t program) = 0;

		#pragma endregion

		#pragma region Uniforms

		// A program keeps the values it is given until they are set again, the
		// way a GL program does. A name the program does not declare is ignored.
		virtual void SetUniform(uint32_t program, const char* name, float value) = 0;
		virtual void SetUniform(uint32_t program, const char* name, const glm::vec3& value) = 0;
		virtual void SetUniform(uint32_t program, const char* name, const glm::vec4& value) = 0;
		virtual void SetUniform(uint32_t program, const char* name, const glm::mat4& value) = 0;

		// Any type but a sampler, which is SetTexture's. components holds the
		// type's columns one after another, four bytes a component: floats, ints
		// or unsigned ints, and a bool as an int.
		virtual void SetUniform(uint32_t program, const char* name, UniformType type, const void* components) = 0;
		virtual void SetTexture(uint32_t program, const char* name, uint32_t texture) = 0;

		#pragma endregion

		#pragma region Drawing

		// Draws vertices laid out as format says as the GL primitive given
		// (GL_TRIANGLES and the rest). usage is the GL buffer usage hint. A
		// program that reads aUV from vertices without one reads the position's
		// x and y, and one that reads aNormal from vertices without one reads
		// none, all zeros.
		virtual void Draw(
			uint32_t program,
			uint32_t primitive,
			const float* vertices,
			size_t count,
			uint32_t usage,
			VertexFormat format = VertexFormat::Position) = 0;

		// Vertices the renderer keeps between draws, for geometry drawn far
		// more often than it changes. SetVertices hands them over, laid out as
		// format says, and DrawVertices draws them as Draw would without
		// handing them over again. 0 is none. usage is the GL buffer usage,
		// which also decides where Vulkan keeps them: GL_STATIC_DRAW ones that
		// go a whole frame without being set again move to the GPU, and the
		// rest are handed over afresh with each draw.
		virtual uint32_t CreateVertices() = 0;
		virtual void SetVertices(uint32_t vertices, const float* data, size_t count, uint32_t usage, const VertexLayout& layout) = 0;
		virtual void DrawVertices(uint32_t program, uint32_t primitive, uint32_t vertices) = 0;
		virtual void DestroyVertices(uint32_t vertices) = 0;

		// The locations a vertex stage's UV layers take, between the vertex
		// attributes and the instance ones, and the words each holds. A layer
		// the vertices lack reads zeros.
		static constexpr uint32_t UVLayerLocation = 4;
		static constexpr uint32_t UVLayerLocations = 7;
		static constexpr uint32_t WordsPerUVLocation = 4;

		// Where an instanced program's vertex stage reads each instance's model
		// matrix, a column a location from the first, and its colour with its
		// seed as the fourth component, since WebGL 2 need give no more than
		// 16 locations.
		static constexpr uint32_t InstanceModelLocation = 11;
		static constexpr uint32_t InstanceColorLocation = 15;

		// What each instance DrawInstances is handed holds, its floats one
		// after another: its model matrix, column by column, its colour, then
		// its seed.
		static constexpr size_t FloatsPerInstance = 20;

		static_assert(UVLayerLocation + UVLayerLocations == InstanceModelLocation);

		// Draws kept vertices once for each of count instances, with a program
		// that reads them at the instance locations.
		virtual void DrawInstances(uint32_t program, uint32_t primitive, uint32_t vertices, const float* instances, size_t count) = 0;

		#pragma endregion

		#pragma region Textures

		// Whether the program samples its colour textures nearest, so each texel
		// draws as a hard-edged square, rather than blending between them, from
		// the textures it is next handed and the next draw on. Off until it is
		// set.
		virtual void SetPixelated(uint32_t program, bool pixelated) = 0;

		// Eight bit RGBA, bottom row first.
		virtual uint32_t CreateTexture(int width, int height, const uint8_t* rgba) = 0;

		virtual int MaxTextureSize() const = 0;

		// A texture as ImGui::Image takes it.
		virtual void* ImGuiTexture(uint32_t texture) = 0;

		#pragma endregion

		#pragma region Targets

		// A depth texture a shader reads through a sampler2DShadow.
		virtual uint32_t CreateDepthTarget(int size) = 0;

		// An RGBA colour texture with a depth buffer behind it.
		virtual uint32_t CreateColorTarget(int width, int height) = 0;

		// The depth behind a colour target, as a texture a sampler2D reads: 0 at
		// the near plane, 1 at the far. It goes when the target does.
		virtual uint32_t TargetDepth(uint32_t target) = 0;

		virtual void DestroyTexture(uint32_t texture) = 0;

		// Draws into a target until it is popped, which puts back whatever was
		// being drawn into before, viewport included.
		virtual void PushTarget(uint32_t target) = 0;
		virtual void PopTarget() = 0;

		#pragma endregion

		#pragma region Render State

		// color is RGBA, or null to leave the colour alone.
		virtual void Clear(const float* color, bool depth) = 0;

		// Pushes the depth of what is drawn next back by its slope, the way
		// glPolygonOffset does. Zeroes turn it off.
		virtual void SetDepthBias(float slope, float constant) = 0;

		// Whether what is drawn next writes colour. Off, it only lays down depth.
		virtual void SetColorWrite(bool write) = 0;

		// Whether what is drawn next passes the depth test at exactly the depth
		// already there as well as nearer, so a second draw of a mesh lands on
		// the depth its first left.
		virtual void SetDepthTestEqual(bool equal) = 0;

		// The size of what is being drawn into.
		virtual glm::ivec2 TargetSize() const = 0;

		#pragma endregion

	protected:
		// Protected as well as the Engine's, so a renderer a test builds with no
		// window can put itself in.
		static inline Renderer* current = nullptr;

	private:
		friend struct Engine;
	};
};
