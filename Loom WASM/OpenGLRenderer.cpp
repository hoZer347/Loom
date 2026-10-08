#include "Renderer.h"

#include "Engine.h"
#include "Input.h"
#include "OpenGL.h"
#include "RenderMath.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "glm/gtc/type_ptr.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>


namespace Loom
{
	namespace
	{
		constexpr int LOG_SIZE = 512;

		// Clamps the shadow sampler's reads to the map, and compares rather than
		// returns depth: linear filtering on a compared texture is a free 2x2 PCF.
		void SetShadowSampling()
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
		};

		void SetColorSampling()
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		};

		GLuint CompileStage(GLenum stage, const char* stageName, const std::string& source)
		{
			const char* text = source.c_str();

			GLuint shader = glCreateShader(stage);
			glShaderSource(shader, 1, &text, nullptr);
			glCompileShader(shader);

			GLint success = 0;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

			if (!success)
			{
				char log[LOG_SIZE];
				glGetShaderInfoLog(shader, LOG_SIZE, nullptr, log);
				glDeleteShader(shader);
				throw std::runtime_error(std::string("Shader compile error in ") + stageName + ":\n" + log);
			};

			return shader;
		};

#ifdef __EMSCRIPTEN__
		void ResizeCanvas()
		{
			int width, height;

			emscripten_get_canvas_element_size("#canvas", &width, &height);
			glfwSetWindowSize(Engine::window, width, height);

			Input::screen_width = width;
			Input::screen_height = height;

			glViewport(0, 0, width, height);
		};
#endif
	};

	struct OpenGLRenderer final : Renderer
	{
		OpenGLRenderer()
		{
#if defined(__EMSCRIPTEN__)
			m_glslVersion = "#version 300 es";
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
			glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#else
			m_glslVersion = "#version 130";
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
		};

		~OpenGLRenderer() override
		{
			if (m_vbo)
				glDeleteBuffers(1, &m_vbo);

#ifndef __EMSCRIPTEN__
			if (m_vao)
				glDeleteVertexArrays(1, &m_vao);
#endif

			for (const auto& [texture, target] : m_targets)
				DeleteTarget(target);
		};

		Backend GetBackend() const override
		{
			return Backend::OpenGL;
		};

		bool Attach(GLFWwindow* window) override
		{
			m_window = window;
			glfwMakeContextCurrent(window);

#ifndef __EMSCRIPTEN__
			glewExperimental = true;

			if (glewInit())
			{
				std::cerr << "Glew failed to init" << std::endl;
				return false;
			};

			glfwSetFramebufferSizeCallback(
				window,
				[](GLFWwindow*, int width, int height)
				{
					glViewport(0, 0, width, height);
				});
#else
			emscripten_set_resize_callback(
				EMSCRIPTEN_EVENT_TARGET_WINDOW,
				nullptr,
				EM_TRUE,
				[](int, const EmscriptenUiEvent*, void*) -> EM_BOOL
				{
					ResizeCanvas();
					return EM_TRUE;
				});
#endif

			glEnable(GL_DEPTH_TEST);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

#if !defined(__EMSCRIPTEN__) && !defined(NDEBUG)
			glEnable(GL_DEBUG_OUTPUT);
			glDebugMessageCallback(
				[](GLenum, GLenum, GLuint, GLenum severity, GLsizei, const GLchar* message, const void*)
				{
					// Notifications are per-buffer-upload chatter from the driver;
					// they drown out anything worth reading (the editor console in
					// particular) without ever saying anything actionable.
					if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
						return;

					std::cerr << "OpenGL Debug: " << message << std::endl;
				},
				nullptr);
#endif

			const GLenum error = glGetError();

			if (error != GL_NO_ERROR)
			{
				std::cerr << "OpenGL error: " << error << std::endl;
				return false;
			};

			std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
			std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;

			glGenBuffers(1, &m_vbo);

#ifndef __EMSCRIPTEN__
			glGenVertexArrays(1, &m_vao);
			glBindVertexArray(m_vao);
#endif

			return true;
		};

		void InitImGui(GLFWwindow* window) override
		{
#ifdef __EMSCRIPTEN__
			// Emscripten: avoid double-callback wiring
			ImGui_ImplGlfw_InitForOpenGL(window, false);
			ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");

			// Make canvas focusable + stop browser stealing input
			emscripten_run_script(R"JS(
			(function(){
				var c = Module['canvas'];
				if (!c) return;
				c.tabIndex = 0;
				c.style.outline = 'none';
				c.style.touchAction = 'none';
				c.focus();
				c.addEventListener('click', () => c.focus());
				c.addEventListener('contextmenu', e => e.preventDefault());
				c.addEventListener('wheel', e => e.preventDefault(), { passive: false });
			})();
		)JS");
#else
			ImGui_ImplGlfw_InitForOpenGL(window, true);
#endif

			ImGui_ImplOpenGL3_Init(m_glslVersion);
		};

		void ShutdownImGui() override
		{
			ImGui_ImplOpenGL3_Shutdown();
			ImGui_ImplGlfw_Shutdown();
		};

		void NewImGuiFrame() override
		{
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
		};

		void RenderImGui(ImDrawData* drawData) override
		{
			ImGui_ImplOpenGL3_RenderDrawData(drawData);
		};

		void RenderImGuiWindows() override
		{
			GLFWwindow* context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(context);
		};

		void ReleaseImGuiFonts() override
		{
			ImGui_ImplOpenGL3_DestroyFontsTexture();
		};

		void BeginFrame() override
		{ };

		void EndFrame() override
		{
			if (m_capture)
			{
				ReadBack(m_capture);
				m_capture = nullptr;
			};

			glFlush();
			glfwSwapBuffers(m_window);
		};

		void Capture(const CaptureHandler& handler) override
		{
			m_capture = handler;
		};

		void ReadBack(const CaptureHandler& handler)
		{
			constexpr int BYTES_PER_PIXEL = 3;

			int width = 0;
			int height = 0;
			glfwGetFramebufferSize(m_window, &width, &height);

			std::vector<uint8_t> pixels((size_t)width * height * BYTES_PER_PIXEL);

			if (!pixels.empty())
			{
				glPixelStorei(GL_PACK_ALIGNMENT, 1);
				glReadBuffer(GL_BACK);
				glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
			};

			handler(width, height, pixels);
		};

		uint32_t CreateProgram(const std::string& name, const std::string& vertex, const std::string& fragment) override
		{
#ifdef __EMSCRIPTEN__
			// GLSL ES gives a fragment shader no default float precision. Desktop
			// GLSL does, so a shader written in the editor would otherwise only
			// compile there.
			const std::string version = "#version 300 es\nprecision highp float;\n";
#else
			const std::string version = "#version 460 core\n";
#endif

			const GLuint vertexShader = CompileStage(GL_VERTEX_SHADER, "VERTEX", version + vertex);
			GLuint fragmentShader = 0;

			try
			{
				fragmentShader = CompileStage(GL_FRAGMENT_SHADER, "FRAGMENT", version + fragment);
			}
			catch (...)
			{
				glDeleteShader(vertexShader);
				throw;
			};

			const GLuint program = glCreateProgram();
			glAttachShader(program, vertexShader);
			glAttachShader(program, fragmentShader);
			glLinkProgram(program);

			glDeleteShader(vertexShader);
			glDeleteShader(fragmentShader);

			GLint success = 0;
			glGetProgramiv(program, GL_LINK_STATUS, &success);

			if (!success)
			{
				char log[LOG_SIZE];
				glGetProgramInfoLog(program, LOG_SIZE, nullptr, log);
				glDeleteProgram(program);
				throw std::runtime_error("Shader link error in " + name + ":\n" + log);
			};

			return program;
		};

		void DeleteProgram(uint32_t program) override
		{
			// A program still bound outlives glDeleteProgram until it is
			// unbound, and anything that puts the binding back afterwards (Clear
			// does) names a program that is gone.
			GLint current = 0;
			glGetIntegerv(GL_CURRENT_PROGRAM, &current);

			if ((GLuint)current == program)
				glUseProgram(0);

			m_units.erase(program);
			glDeleteProgram(program);
		};

		void SetUniform(uint32_t program, const char* name, float value) override
		{
			glUseProgram(program);
			glUniform1f(glGetUniformLocation(program, name), value);
		};

		void SetUniform(uint32_t program, const char* name, const glm::vec3& value) override
		{
			glUseProgram(program);
			glUniform3fv(glGetUniformLocation(program, name), 1, glm::value_ptr(value));
		};

		void SetUniform(uint32_t program, const char* name, const glm::vec4& value) override
		{
			glUseProgram(program);
			glUniform4fv(glGetUniformLocation(program, name), 1, glm::value_ptr(value));
		};

		void SetUniform(uint32_t program, const char* name, const glm::mat4& value) override
		{
			glUseProgram(program);
			glUniformMatrix4fv(glGetUniformLocation(program, name), 1, GL_FALSE, glm::value_ptr(value));
		};

		void SetUniform(uint32_t program, const char* name, UniformType type, const void* components) override
		{
			glUseProgram(program);

			const GLint location = glGetUniformLocation(program, name);
			const GLfloat* floats = (const GLfloat*)components;
			const GLint* ints = (const GLint*)components;
			const GLuint* uints = (const GLuint*)components;

			switch (type)
			{
			case UniformType::Float:	glUniform1fv(location, 1, floats);	break;
			case UniformType::Vec2:		glUniform2fv(location, 1, floats);	break;
			case UniformType::Vec3:		glUniform3fv(location, 1, floats);	break;
			case UniformType::Vec4:		glUniform4fv(location, 1, floats);	break;
			case UniformType::Int:
			case UniformType::Bool:		glUniform1iv(location, 1, ints);	break;
			case UniformType::IVec2:
			case UniformType::BVec2:	glUniform2iv(location, 1, ints);	break;
			case UniformType::IVec3:
			case UniformType::BVec3:	glUniform3iv(location, 1, ints);	break;
			case UniformType::IVec4:
			case UniformType::BVec4:	glUniform4iv(location, 1, ints);	break;
			case UniformType::UInt:		glUniform1uiv(location, 1, uints);	break;
			case UniformType::UVec2:	glUniform2uiv(location, 1, uints);	break;
			case UniformType::UVec3:	glUniform3uiv(location, 1, uints);	break;
			case UniformType::UVec4:	glUniform4uiv(location, 1, uints);	break;
			case UniformType::Mat2:		glUniformMatrix2fv(location, 1, GL_FALSE, floats);		break;
			case UniformType::Mat2x3:	glUniformMatrix2x3fv(location, 1, GL_FALSE, floats);	break;
			case UniformType::Mat2x4:	glUniformMatrix2x4fv(location, 1, GL_FALSE, floats);	break;
			case UniformType::Mat3x2:	glUniformMatrix3x2fv(location, 1, GL_FALSE, floats);	break;
			case UniformType::Mat3:		glUniformMatrix3fv(location, 1, GL_FALSE, floats);		break;
			case UniformType::Mat3x4:	glUniformMatrix3x4fv(location, 1, GL_FALSE, floats);	break;
			case UniformType::Mat4x2:	glUniformMatrix4x2fv(location, 1, GL_FALSE, floats);	break;
			case UniformType::Mat4x3:	glUniformMatrix4x3fv(location, 1, GL_FALSE, floats);	break;
			case UniformType::Mat4:		glUniformMatrix4fv(location, 1, GL_FALSE, floats);		break;
			case UniformType::Sampler2D:	break;
			};
		};

		void SetTexture(uint32_t program, const char* name, uint32_t texture) override
		{
			// Each sampler a program names gets a unit of its own, in the order
			// they are first set.
			Units& units = m_units[program];
			auto found = units.find(std::string_view(name));

			if (found == units.end())
				found = units.emplace(name, (GLint)units.size()).first;

			glUseProgram(program);
			glActiveTexture(GL_TEXTURE0 + found->second);
			glBindTexture(GL_TEXTURE_2D, texture);
			glActiveTexture(GL_TEXTURE0);

			glUniform1i(glGetUniformLocation(program, name), found->second);
		};

		void Draw(uint32_t program, uint32_t primitive, const float* positions, size_t vertices, uint32_t usage) override
		{
			constexpr GLint COMPONENTS = 3;

			glUseProgram(program);

			glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
			glBufferData(GL_ARRAY_BUFFER, vertices * COMPONENTS * sizeof(float), positions, usage);

			// Left enabled after the draw: toggling it per draw has the driver
			// recompile the vertex shader for each state.
			glVertexAttribPointer(0, COMPONENTS, GL_FLOAT, GL_FALSE, COMPONENTS * sizeof(float), (void*)0);
			glEnableVertexAttribArray(0);

			glDrawArrays(primitive, 0, (GLsizei)vertices);

			glBindBuffer(GL_ARRAY_BUFFER, 0);
		};

		uint32_t CreateTexture(int width, int height, const uint8_t* rgba) override
		{
			const GLuint bound = BoundTexture();

			GLuint texture = 0;
			glGenTextures(1, &texture);
			glBindTexture(GL_TEXTURE_2D, texture);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
			SetColorSampling();
			glBindTexture(GL_TEXTURE_2D, bound);

			return texture;
		};

		uint32_t CreateDepthTarget(int size) override
		{
			const GLuint previous = BoundFramebuffer();
			const GLuint bound = BoundTexture();
			Target target;

			glGenTextures(1, &target.texture);
			glBindTexture(GL_TEXTURE_2D, target.texture);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
			SetShadowSampling();
			glBindTexture(GL_TEXTURE_2D, bound);

			glGenFramebuffers(1, &target.framebuffer);
			glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, target.texture, 0);

			// Depth only: without these a framebuffer with no colour attachment is
			// incomplete.
			const GLenum none = GL_NONE;
			glDrawBuffers(1, &none);
			glReadBuffer(GL_NONE);

			return Complete(target, size, size, previous);
		};

		uint32_t CreateColorTarget(int width, int height) override
		{
			const GLuint previous = BoundFramebuffer();
			const GLuint bound = BoundTexture();
			Target target;

			glGenTextures(1, &target.texture);
			glBindTexture(GL_TEXTURE_2D, target.texture);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			SetColorSampling();
			glBindTexture(GL_TEXTURE_2D, bound);

			glGenFramebuffers(1, &target.framebuffer);
			glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);

			glGenRenderbuffers(1, &target.depth);
			glBindRenderbuffer(GL_RENDERBUFFER, target.depth);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target.depth);
			glBindRenderbuffer(GL_RENDERBUFFER, 0);

			return Complete(target, width, height, previous);
		};

		void DestroyTexture(uint32_t texture) override
		{
			const auto found = m_targets.find(texture);

			if (found == m_targets.end())
			{
				glDeleteTextures(1, &texture);
				return;
			};

			DeleteTarget(found->second);
			m_targets.erase(found);
		};

		void PushTarget(uint32_t texture) override
		{
			Saved saved;
			glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved.framebuffer);
			glGetIntegerv(GL_VIEWPORT, saved.viewport);
			m_saved.push_back(saved);

			const Target& target = m_targets.at(texture);
			glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
			glViewport(0, 0, target.width, target.height);
		};

		void PopTarget() override
		{
			const Saved saved = m_saved.back();
			m_saved.pop_back();

			glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved.framebuffer);
			glViewport(
				saved.viewport[VIEWPORT_X],
				saved.viewport[VIEWPORT_Y],
				saved.viewport[VIEWPORT_WIDTH],
				saved.viewport[VIEWPORT_HEIGHT]);
		};

		void Clear(const float* color, bool depth) override
		{
			GLbitfield mask = 0;

			if (color)
			{
				glClearColor(color[0], color[1], color[2], color[3]);
				mask |= GL_COLOR_BUFFER_BIT;
			};

			if (depth)
				mask |= GL_DEPTH_BUFFER_BIT;

			// Not with a program bound: drivers validate it on a clear as they
			// would on a draw, so clearing a target mid-scene, before that
			// program's textures are set, is reported as undefined behaviour.
			GLint program = 0;
			glGetIntegerv(GL_CURRENT_PROGRAM, &program);
			glUseProgram(0);

			glClear(mask);

			glUseProgram((GLuint)program);
		};

		void SetDepthBias(float slope, float constant) override
		{
			if (slope == 0.0f && constant == 0.0f)
			{
				glDisable(GL_POLYGON_OFFSET_FILL);
				return;
			};

			glEnable(GL_POLYGON_OFFSET_FILL);
			glPolygonOffset(slope, constant);
		};

		glm::ivec2 TargetSize() const override
		{
			GLint viewport[VIEWPORT_SIZE]{ };
			glGetIntegerv(GL_VIEWPORT, viewport);

			return glm::ivec2(viewport[VIEWPORT_WIDTH], viewport[VIEWPORT_HEIGHT]);
		};

		int MaxTextureSize() const override
		{
			GLint size = 0;
			glGetIntegerv(GL_MAX_TEXTURE_SIZE, &size);

			return size;
		};

		void* ImGuiTexture(uint32_t texture) override
		{
			return (void*)(intptr_t)texture;
		};

	private:
		struct Target
		{
			GLuint texture = 0;
			GLuint framebuffer = 0;
			GLuint depth = 0;
			GLsizei width = 0;
			GLsizei height = 0;
		};

		struct Saved
		{
			GLint framebuffer = 0;
			GLint viewport[VIEWPORT_SIZE]{ };
		};

		// Creating a texture binds it, and whatever program is in use may be
		// sampling the one it replaces on that unit.
		static GLuint BoundTexture()
		{
			GLint texture = 0;
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);

			return (GLuint)texture;
		};

		static GLuint BoundFramebuffer()
		{
			GLint framebuffer = 0;
			glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);

			return (GLuint)framebuffer;
		};

		// Creating a target binds its framebuffer, so whatever was being drawn
		// into is bound again after.
		uint32_t Complete(Target& target, int width, int height, GLuint previous)
		{
			const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

			glBindFramebuffer(GL_FRAMEBUFFER, previous);

			if (!complete)
			{
				std::cerr << "A " << width << 'x' << height << " render target is incomplete" << std::endl;
				DeleteTarget(target);
				return 0;
			};

			target.width = width;
			target.height = height;
			m_targets[target.texture] = target;

			return target.texture;
		};

		static void DeleteTarget(const Target& target)
		{
			if (target.framebuffer)	glDeleteFramebuffers(1, &target.framebuffer);
			if (target.texture)		glDeleteTextures(1, &target.texture);
			if (target.depth)		glDeleteRenderbuffers(1, &target.depth);
		};

		GLFWwindow* m_window = nullptr;
		const char* m_glslVersion = nullptr;

		CaptureHandler m_capture;

		GLuint m_vbo = 0;
		GLuint m_vao = 0;

		std::unordered_map<uint32_t, Target> m_targets;
		std::vector<Saved> m_saved;

		// Looked up by the const char* a caller has, without building a string.
		struct NameHash
		{
			using is_transparent = void;

			size_t operator()(std::string_view name) const { return std::hash<std::string_view>{ }(name); };
		};

		typedef std::unordered_map<std::string, GLint, NameHash, std::equal_to<>> Units;

		std::unordered_map<uint32_t, Units> m_units;
	};

	std::unique_ptr<Renderer> CreateOpenGLRenderer()
	{
		return std::make_unique<OpenGLRenderer>();
	};
};
