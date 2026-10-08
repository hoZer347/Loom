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
	enum class Backend { OpenGL, Vulkan };

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
		// The pixels of a finished frame, red first and bottom row first.
		typedef std::function<void(int width, int height, const std::vector<uint8_t>& rgb)> CaptureHandler;

		// Builds the renderer for a backend and sets the window hints it needs,
		// which is why it comes before the window does.
		static std::unique_ptr<Renderer> Create(Backend backend);

		// The renderer the Engine opened, or null before one has (a test with no
		// window).
		static Renderer* Get() { return current; };

		virtual ~Renderer() = default;

		virtual Backend GetBackend() const = 0;

		// Takes the window over. False when the API cannot run on it, which for
		// Vulkan means the Engine falls back to OpenGL.
		virtual bool Attach(GLFWwindow* window) = 0;

		virtual void InitImGui(GLFWwindow* window) = 0;
		virtual void ShutdownImGui() = 0;
		virtual void NewImGuiFrame() = 0;
		virtual void RenderImGui(ImDrawData* drawData) = 0;

		// The ImGui windows dragged out of the main one.
		virtual void RenderImGuiWindows() = 0;

		// Drops ImGui's font texture, which the next frame builds again from
		// the font atlas. For after the atlas has changed, between frames.
		virtual void ReleaseImGuiFonts() = 0;

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

		// Draws positions, three floats a vertex, as the GL primitive given
		// (GL_TRIANGLES and the rest). usage is the GL buffer usage hint.
		virtual void Draw(uint32_t program, uint32_t primitive, const float* positions, size_t vertices, uint32_t usage) = 0;

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

		// The size of what is being drawn into.
		virtual glm::ivec2 TargetSize() const = 0;

		virtual int MaxTextureSize() const = 0;

		// A texture as ImGui::Image takes it.
		virtual void* ImGuiTexture(uint32_t texture) = 0;

	private:
		friend struct Engine;

		static inline Renderer* current = nullptr;
	};
};
