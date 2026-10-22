#include "Renderer.h"

#include "Engine.h"
#include "Input.h"
#include "OpenGL.h"
#include "Profiler.h"
#include "RenderMath.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

		void SetNearestSampling(GLuint sampler)
		{
			glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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

			Input::OnResize(width, height);

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

			if (m_noNormal)
				glDeleteBuffers(1, &m_noNormal);

			for (const auto& [buffer, kept] : m_kept)
				glDeleteBuffers(1, &buffer);

			if (m_nearest)
				glDeleteSamplers(1, &m_nearest);

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

			glfwSwapInterval(Engine::vsync ? 1 : 0);

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

			constexpr float NO_NORMAL[3] = { };
			glGenBuffers(1, &m_noNormal);
			glBindBuffer(GL_ARRAY_BUFFER, m_noNormal);
			glBufferData(GL_ARRAY_BUFFER, sizeof(NO_NORMAL), NO_NORMAL, GL_STATIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			glGenSamplers(1, &m_nearest);
			SetNearestSampling(m_nearest);

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

		void BeginFrame() override
		{ };

		void EndFrame() override
		{
			if (m_capture)
			{
				ReadBack(m_capture);
				m_capture = nullptr;
			};

			{
				LOOM_STAGE("glFlush");
				glFlush();
			};

			LOOM_STAGE("glfwSwapBuffers");
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

			m_programs.erase(program);
			m_pixelated.erase(program);
			glDeleteProgram(program);
		};

		void SetUniform(uint32_t program, const char* name, float value) override
		{
			glUseProgram(program);
			glUniform1f(Location(program, name), value);
		};

		void SetUniform(uint32_t program, const char* name, const glm::vec3& value) override
		{
			glUseProgram(program);
			glUniform3fv(Location(program, name), 1, glm::value_ptr(value));
		};

		void SetUniform(uint32_t program, const char* name, const glm::vec4& value) override
		{
			glUseProgram(program);
			glUniform4fv(Location(program, name), 1, glm::value_ptr(value));
		};

		void SetUniform(uint32_t program, const char* name, const glm::mat4& value) override
		{
			glUseProgram(program);
			glUniformMatrix4fv(Location(program, name), 1, GL_FALSE, glm::value_ptr(value));
		};

		void SetUniform(uint32_t program, const char* name, UniformType type, const void* components) override
		{
			glUseProgram(program);

			const GLint location = Location(program, name);
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
			Program& state = m_programs[program];
			auto found = state.units.find(std::string_view(name));

			if (found == state.units.end())
			{
				found = state.units.emplace(name, (GLint)state.textures.size()).first;
				state.textures.push_back(0);

				glUseProgram(program);
				glUniform1i(Location(program, name), found->second);
			};

			state.textures[found->second] = texture;
		};

		// Where the program's uniform is, asked of GL once.
		GLint Location(uint32_t program, const char* name)
		{
			Locations& locations = m_programs[program].locations;
			auto found = locations.find(std::string_view(name));

			if (found == locations.end())
				found = locations.emplace(name, glGetUniformLocation(program, name)).first;

			return found->second;
		};

		// Binds the program's textures to their units, which every program
		// shares, as Vulkan hands each draw its program's own.
		void BindTextures(uint32_t program)
		{
			const auto found = m_programs.find(program);

			if (found == m_programs.end())
				return;

			const std::vector<GLuint>& textures = found->second.textures;
			const bool pixelated = m_pixelated.contains(program);

			for (GLint unit = 0; unit < (GLint)textures.size(); unit++)
			{
				glActiveTexture(GL_TEXTURE0 + unit);
				glBindTexture(GL_TEXTURE_2D, textures[unit]);

				// A sampler object overrides all of a texture's sampling, a shadow
				// map's comparison included, so shadow maps keep their own.
				const auto target = m_targets.find(textures[unit]);
				const bool shadow = target != m_targets.end() && target->second.compare;
				glBindSampler(unit, pixelated && !shadow ? m_nearest : 0);
			};

			glActiveTexture(GL_TEXTURE0);
		};

		void Draw(uint32_t program, uint32_t primitive, const float* vertices, size_t count, uint32_t usage, VertexFormat format) override
		{
			glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
			glBufferData(GL_ARRAY_BUFFER, count * FloatsPerVertex(format) * sizeof(float), vertices, usage);

			DrawBuffer(program, primitive, m_vbo, count, format);
		};

		uint32_t CreateVertices() override
		{
			GLuint buffer = 0;
			glGenBuffers(1, &buffer);

			m_kept[buffer] = { };

			return buffer;
		};

		void SetVertices(uint32_t vertices, const float* data, size_t count, uint32_t usage, VertexFormat format) override
		{
			const auto found = m_kept.find(vertices);

			if (found == m_kept.end())
				return;

			found->second = { count, format };

			glBindBuffer(GL_ARRAY_BUFFER, vertices);
			glBufferData(GL_ARRAY_BUFFER, count * FloatsPerVertex(format) * sizeof(float), data, usage);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		};

		void DrawVertices(uint32_t program, uint32_t primitive, uint32_t vertices) override
		{
			const auto found = m_kept.find(vertices);

			if (found != m_kept.end())
				DrawBuffer(program, primitive, vertices, found->second.count, found->second.format);
		};

		void DestroyVertices(uint32_t vertices) override
		{
			if (m_kept.erase(vertices))
				glDeleteBuffers(1, &vertices);
		};

		// Draws count vertices laid out as format says from buffer, which holds them already.
		void DrawBuffer(uint32_t program, uint32_t primitive, GLuint buffer, size_t count, VertexFormat format)
		{
			constexpr GLint POSITION_COMPONENTS = 3;
			constexpr GLint UV_COMPONENTS = 2;
			constexpr GLint NORMAL_COMPONENTS = 3;
			constexpr GLuint POSITION_LOCATION = 0;
			constexpr GLuint UV_LOCATION = 1;
			constexpr GLuint NORMAL_LOCATION = 2;

			// A divisor of 1 reads one value for the whole of a draw of one instance.
			constexpr GLuint PER_VERTEX = 0;
			constexpr GLuint PER_DRAW = 1;

			const GLsizei stride = (GLsizei)(FloatsPerVertex(format) * sizeof(float));

			glUseProgram(program);
			BindTextures(program);

			glBindBuffer(GL_ARRAY_BUFFER, buffer);

			// All left enabled after the draw: toggling them per draw has the
			// driver recompile the vertex shader for each state. Without texture
			// coordinates, aUV reads the position's x and y, and without normals,
			// aNormal reads m_noNormal's zeros for every vertex.
			const GLsizeiptr uvOffset = format != VertexFormat::Position ? POSITION_COMPONENTS * sizeof(float) : 0;
			const GLsizeiptr normalOffset = (POSITION_COMPONENTS + UV_COMPONENTS) * sizeof(float);

			glVertexAttribPointer(POSITION_LOCATION, POSITION_COMPONENTS, GL_FLOAT, GL_FALSE, stride, (void*)0);
			glEnableVertexAttribArray(POSITION_LOCATION);
			glVertexAttribPointer(UV_LOCATION, UV_COMPONENTS, GL_FLOAT, GL_FALSE, stride, (void*)uvOffset);
			glEnableVertexAttribArray(UV_LOCATION);

			if (format == VertexFormat::PositionUVNormal)
			{
				glVertexAttribPointer(NORMAL_LOCATION, NORMAL_COMPONENTS, GL_FLOAT, GL_FALSE, stride, (void*)normalOffset);
				glVertexAttribDivisor(NORMAL_LOCATION, PER_VERTEX);
			}
			else
			{
				glBindBuffer(GL_ARRAY_BUFFER, m_noNormal);
				glVertexAttribPointer(NORMAL_LOCATION, NORMAL_COMPONENTS, GL_FLOAT, GL_FALSE, 0, (void*)0);
				glVertexAttribDivisor(NORMAL_LOCATION, PER_DRAW);
			};

			glEnableVertexAttribArray(NORMAL_LOCATION);

			glDrawArrays(primitive, 0, (GLsizei)count);

			glBindBuffer(GL_ARRAY_BUFFER, 0);
		};

		void SetPixelated(uint32_t program, bool pixelated) override
		{
			if (pixelated)
				m_pixelated.insert(program);
			else m_pixelated.erase(program);
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
			target.compare = true;

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
			// GL names a texture anew once it is gone, so no program may still bind it.
			for (auto& [program, state] : m_programs)
				std::replace(state.textures.begin(), state.textures.end(), (GLuint)texture, 0u);

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

		void SetColorWrite(bool write) override
		{
			const GLboolean mask = write ? GL_TRUE : GL_FALSE;
			glColorMask(mask, mask, mask, mask);
		};

		void SetDepthTestEqual(bool equal) override
		{
			glDepthFunc(equal ? GL_LEQUAL : GL_LESS);
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

			// A shadow map, sampled by comparing.
			bool compare = false;
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

		// What CreateVertices handed out, by GL name: how many vertices each
		// holds and how they are laid out.
		struct Kept
		{
			size_t count = 0;
			VertexFormat format = VertexFormat::Position;
		};

		std::unordered_map<GLuint, Kept> m_kept;

		// A normal of zeros, what aNormal reads from vertices without normals.
		GLuint m_noNormal = 0;

		// What a pixelated program samples its colour textures through.
		GLuint m_nearest = 0;
		std::unordered_set<uint32_t> m_pixelated;

		std::unordered_map<uint32_t, Target> m_targets;
		std::vector<Saved> m_saved;

		// Looked up by the const char* a caller has, without building a string.
		struct NameHash
		{
			using is_transparent = void;

			size_t operator()(std::string_view name) const { return std::hash<std::string_view>{ }(name); };
		};

		typedef std::unordered_map<std::string, GLint, NameHash, std::equal_to<>> Locations;

		// What each program has been given: where its uniforms are, the unit
		// each sampler has, and the texture set for each unit.
		struct Program
		{
			Locations locations;
			Locations units;
			std::vector<GLuint> textures;
		};

		std::unordered_map<uint32_t, Program> m_programs;
	};

	std::unique_ptr<Renderer> CreateOpenGLRenderer()
	{
		return std::make_unique<OpenGLRenderer>();
	};
};
