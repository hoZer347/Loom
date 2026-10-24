#pragma once

#include "Loom API.h"

#include "UniformType.h"

#include "glm/glm.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;
struct ImDrawData;


namespace Loom
{
	enum class Backend { OpenGL, Vulkan, WebGPU };

	// What each vertex handed to Renderer::Draw holds, its floats one after
	// another: a position at location 0 (aPos), with PositionUV a texture
	// coordinate after it at location 1 (aUV), with PositionUVNormal a
	// normal after that at location 2 (aNormal), and with
	// PositionUVNormalColor a colour after that at location 3 (aColor, a
	// vec4). A shader reading aColor from vertices without one reads white.
	enum class VertexFormat { Position, PositionUV, PositionUVNormal, PositionUVNormalColor };

	LOOM_API size_t FloatsPerVertex(VertexFormat format);

	/**
	* Loom::Renderer
	* - The graphics API behind everything the engine draws, picked once when
	*   the Engine opens its window (Engine::backend). Desktop builds run on
	*   Vulkan or OpenGL, and the web on WebGPU or WebGL2
	* - Compute runs the same GLSL everywhere it is supported, which is every
	*   backend but WebGL2 and a desktop GL older than 4.3
	* - Behaves like GL whichever is underneath: shaders are the same GLSL with
	*   loose uniforms, clip space is GL's, and a target reads back bottom row
	*   first. A scene, a script or a shader cannot tell which it is running on
	* - Programs and textures are plain handles, 0 meaning none. Under GL they
	*   are the GL names themselves, so raw GL code can keep using them
	*/
	struct LOOM_API Renderer
	{
		// The pixels of a finished frame, red first and bottom row first.
		typedef std::function<void(int width, int height, const std::vector<uint8_t>& rgb)> CaptureHandler;

		// A storage buffer's bytes.
		typedef std::function<void(const std::vector<uint8_t>& data)> BufferHandler;

		// Builds the renderer for a backend and sets the window hints it needs,
		// which is why it comes before the window does.
		static std::unique_ptr<Renderer> Create(Backend backend);

		// The renderer the Engine opened, or null before one has (a test with no
		// window).
		static Renderer* Get() { return current; };

		virtual ~Renderer() = default;

		virtual Backend GetBackend() const = 0;

		// Takes the window over. False when the API cannot run on it, which for
		// Vulkan or WebGPU means the Engine falls back to OpenGL.
		virtual bool Attach(GLFWwindow* window) = 0;

		virtual void InitImGui(GLFWwindow* window) = 0;
		virtual void ShutdownImGui() = 0;
		virtual void NewImGuiFrame() = 0;
		virtual void RenderImGui(ImDrawData* drawData) = 0;

		// The ImGui windows dragged out of the main one.
		virtual void RenderImGuiWindows() = 0;

		// Starts a frame on the window, which is the target until something is
		// pushed over it, and presents it.
		virtual void BeginFrame() = 0;
		virtual void EndFrame() = 0;

		// Hands the window's pixels to the handler once everything drawn this
		// frame has landed, as the frame ends.
		virtual void Capture(const CaptureHandler& handler) = 0;

		// Compiles the two stages, which carry no #version line. Throws
		// std::runtime_error with the compiler's log when either fails.
		virtual uint32_t CreateProgram(const std::string& name, const std::string& vertex, const std::string& fragment) = 0;
		virtual void DeleteProgram(uint32_t program) = 0;

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

		virtual bool SupportsCompute() const = 0;

		// Compiles a compute stage, which carries no #version line. Throws
		// std::runtime_error with the compiler's log when it fails, or when the
		// backend has no compute. Deleted with DeleteProgram, and handed its
		// uniforms and samplers as a draw program is.
		virtual uint32_t CreateComputeProgram(const std::string& name, const std::string& compute) = 0;

		// Runs the program over that many work groups in each dimension. What it
		// writes is there for everything recorded after it.
		virtual void Dispatch(uint32_t program, uint32_t x, uint32_t y, uint32_t z) = 0;

		// A std430 storage buffer of size bytes, zeroed, or 0 for none.
		virtual uint32_t CreateBuffer(size_t size) = 0;
		virtual void SetBufferData(uint32_t buffer, const void* data, size_t size, size_t offset = 0) = 0;

		// Hands the buffer's bytes as they are at the call, once the frame ends.
		virtual void ReadBuffer(uint32_t buffer, const BufferHandler& handler) = 0;
		virtual void DestroyBuffer(uint32_t buffer) = 0;

		// Binds a buffer to the program's storage block of that type name, for
		// a draw program as for a compute one. A vertex stage's blocks must be
		// readonly, since WebGPU writes none from there. A draw or dispatch
		// with a block or image not set to a buffer or storage texture that is
		// still there does nothing, as does Dispatch with a draw program or a
		// draw with a compute one.
		virtual void SetBuffer(uint32_t program, const char* block, uint32_t buffer) = 0;

		// An RGBA8 texture a compute program writes through a writeonly rgba8
		// image2D and a draw program samples. imageStore at (x, y) lands where
		// texture() reads ((x + 0.5) / width, (y + 0.5) / height). Freed with
		// DestroyTexture.
		virtual uint32_t CreateStorageTexture(int width, int height) = 0;
		virtual void SetImage(uint32_t program, const char* name, uint32_t texture) = 0;

		// Draws vertices laid out as format says as the GL primitive given
		// (GL_TRIANGLES and the rest). usage is the GL buffer usage hint. A
		// program that reads aUV from vertices without one reads the position's
		// x and y, and one that reads aNormal from vertices without one reads
		// none, all zeros. Null vertices draws count with no vertex data, for a
		// vertex stage that reads gl_VertexID.
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
		virtual void SetVertices(uint32_t vertices, const float* data, size_t count, uint32_t usage, VertexFormat format) = 0;
		virtual void DrawVertices(uint32_t program, uint32_t primitive, uint32_t vertices) = 0;
		virtual void DestroyVertices(uint32_t vertices) = 0;

		// Where an instanced program's vertex stage reads each instance's model
		// matrix, a column a location from the first, and its colour.
		static constexpr uint32_t InstanceModelLocation = 11;
		static constexpr uint32_t InstanceColorLocation = 15;

		// What each instance DrawInstances is handed holds, its floats one
		// after another: its model matrix, column by column, then its colour.
		static constexpr size_t FloatsPerInstance = 19;

		// Draws kept vertices once for each of count instances, with a program
		// that reads them at the instance locations.
		virtual void DrawInstances(uint32_t program, uint32_t primitive, uint32_t vertices, const float* instances, size_t count) = 0;

		// Whether the program samples its colour textures nearest, so each texel
		// draws as a hard-edged square, rather than blending between them, from
		// the textures it is next handed and the next draw on. Off until it is
		// set.
		virtual void SetPixelated(uint32_t program, bool pixelated) = 0;

		// Eight bit RGBA, bottom row first.
		virtual uint32_t CreateTexture(int width, int height, const uint8_t* rgba) = 0;

		// A depth texture a shader reads through a sampler2DShadow.
		virtual uint32_t CreateDepthTarget(int size) = 0;

		// An RGBA colour texture with a depth buffer behind it.
		virtual uint32_t CreateColorTarget(int width, int height) = 0;

		virtual void DestroyTexture(uint32_t texture) = 0;

		// Draws into a target until it is popped, which puts back whatever was
		// being drawn into before, viewport included.
		virtual void PushTarget(uint32_t target) = 0;
		virtual void PopTarget() = 0;

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

		virtual int MaxTextureSize() const = 0;

		// A texture as ImGui::Image takes it.
		virtual void* ImGuiTexture(uint32_t texture) = 0;

	protected:
		// Protected as well as the Engine's, so a renderer a test builds with no
		// window can put itself in.
		static inline Renderer* current = nullptr;

	private:
		friend struct Engine;
	};
};
