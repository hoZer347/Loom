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
#include <limits>
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

		// A UV layer slot a program's vertex stage reads, and where.
		struct UVBinding
		{
			UVSlot slot;
			GLuint location = 0;
		};

		// Where the program reads the UV layer slots its vertex stage uses.
		std::vector<UVBinding> UVSlotsOf(GLuint program)
		{
			GLint attributes = 0;
			GLint longest = 0;
			glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &attributes);
			glGetProgramiv(program, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &longest);

			std::vector<UVBinding> slots;
			std::vector<GLchar> name((size_t)longest);

			for (GLint i = 0; i < attributes; i++)
			{
				GLsizei length = 0;
				GLint size = 0;
				GLenum type = 0;
				glGetActiveAttrib(program, (GLuint)i, longest, &length, &size, &type, name.data());

				const std::string attribute(name.data(), (size_t)length);

				const std::optional<UVSlot> slot = ParseUVSlot(attribute);
				const GLint location = slot ? glGetAttribLocation(program, attribute.c_str()) : -1;

				if (location >= (GLint)Renderer::UVLayerLocation && location < (GLint)Renderer::InstanceModelLocation)
					slots.push_back({ *slot, (GLuint)location });
			};

			return slots;
		};

		// Clamped and compared: linear filtering on a compared texture is a free 2x2 PCF.
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

		// WebGL2 leaves a filtered depth texture incomplete.
		void SetDepthSampling()
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
		#pragma region Lifetime

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

			if (m_instances)
				glDeleteBuffers(1, &m_instances);

			if (m_noNormal)
				glDeleteBuffers(1, &m_noNormal);

			if (m_noColor)
				glDeleteBuffers(1, &m_noColor);

			if (m_noUV)
				glDeleteBuffers(1, &m_noUV);

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

		#pragma endregion

		#pragma region Window

		bool Attach(GLFWwindow* window) override
		{
			m_window = window;
			glfwMakeContextCurrent(window);

#ifndef __EMSCRIPTEN__
			glewExperimental = true;

			if (glewInit())
			{
				std::cerr << "OpenGL: GLEW could not start" << std::endl;
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
				[](GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message, const void*)
				{
					// Notifications and performance warnings are noise that drowns out real messages.
					if (severity == GL_DEBUG_SEVERITY_NOTIFICATION || type == GL_DEBUG_TYPE_PERFORMANCE)
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

			glGenBuffers(1, &m_vbo);
			glGenBuffers(1, &m_instances);

			constexpr float NO_NORMAL[3] = { };
			glGenBuffers(1, &m_noNormal);
			glBindBuffer(GL_ARRAY_BUFFER, m_noNormal);
			glBufferData(GL_ARRAY_BUFFER, sizeof(NO_NORMAL), NO_NORMAL, GL_STATIC_DRAW);

			constexpr float NO_COLOR[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			glGenBuffers(1, &m_noColor);
			glBindBuffer(GL_ARRAY_BUFFER, m_noColor);
			glBufferData(GL_ARRAY_BUFFER, sizeof(NO_COLOR), NO_COLOR, GL_STATIC_DRAW);

			constexpr GLuint NO_UV[WordsPerUVLocation] = { };
			glGenBuffers(1, &m_noUV);
			glBindBuffer(GL_ARRAY_BUFFER, m_noUV);
			glBufferData(GL_ARRAY_BUFFER, sizeof(NO_UV), NO_UV, GL_STATIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			glGenSamplers(1, &m_nearest);
			SetNearestSampling(m_nearest);

#ifndef __EMSCRIPTEN__
			glGenVertexArrays(1, &m_vao);
			glBindVertexArray(m_vao);
#endif

			return true;
		};

		#pragma endregion

		#pragma region ImGui

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

		#pragma endregion

		#pragma region Frames

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

		#pragma endregion

		#pragma region Programs

		uint32_t CreateProgram(const std::string& name, const std::string& vertex, const std::string& fragment) override
		{
#ifdef __EMSCRIPTEN__
			// GLSL ES gives fragment shaders no default float precision.
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

			m_programs[program].uvSlots = UVSlotsOf(program);

			return program;
		};

		void DeleteProgram(uint32_t program) override
		{
			// A bound program outlives glDeleteProgram, and a restored binding would name it.
			GLint current = 0;
			glGetIntegerv(GL_CURRENT_PROGRAM, &current);

			if ((GLuint)current == program)
				glUseProgram(0);

			m_programs.erase(program);
			m_pixelated.erase(program);
			glDeleteProgram(program);
		};

		#pragma endregion

		#pragma region Uniforms

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

		void SetUniforms(uint32_t program, const char* name, const glm::mat4* values, size_t count) override
		{
			glUseProgram(program);
			glUniformMatrix4fv(Location(program, name), (GLsizei)count, GL_FALSE, glm::value_ptr(*values));
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
			// Each sampler gets its own unit, in the order first set.
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

		// Units are shared by every program, where Vulkan hands each draw its own.
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

				// A sampler object overrides a shadow map's comparison, so shadow maps keep their own.
				const auto target = m_targets.find(textures[unit]);
				const bool shadow = target != m_targets.end() && target->second.compare;
				glBindSampler(unit, pixelated && !shadow ? m_nearest : 0);
			};

			glActiveTexture(GL_TEXTURE0);
		};

		#pragma endregion

		#pragma region Drawing

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

		void SetVertices(uint32_t vertices, const float* data, size_t count, uint32_t usage, const VertexLayout& layout) override
		{
			const auto found = m_kept.find(vertices);

			if (found == m_kept.end())
				return;

			found->second = { count, layout };

			glBindBuffer(GL_ARRAY_BUFFER, vertices);
			glBufferData(GL_ARRAY_BUFFER, count * FloatsPerVertex(layout) * sizeof(float), data, usage);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		};

		void DrawVertices(uint32_t program, uint32_t primitive, uint32_t vertices) override
		{
			const auto found = m_kept.find(vertices);

			if (found != m_kept.end())
				DrawBuffer(program, primitive, vertices, found->second.count, found->second.layout);
		};

		void DrawInstances(uint32_t program, uint32_t primitive, uint32_t vertices, const float* instances, size_t count) override
		{
			const auto found = m_kept.find(vertices);

			if (found != m_kept.end())
				DrawBuffer(program, primitive, vertices, found->second.count, found->second.layout, instances, count);
		};

		void DestroyVertices(uint32_t vertices) override
		{
			if (m_kept.erase(vertices))
				glDeleteBuffers(1, &vertices);
		};

		// From buffer, once or once per instance.
		void DrawBuffer(uint32_t program, uint32_t primitive, GLuint buffer, size_t count, const VertexLayout& layout, const float* instances = nullptr, size_t instanceCount = 0)
		{
			constexpr GLint POSITION_COMPONENTS = 3;
			constexpr GLint UV_COMPONENTS = 2;
			constexpr GLint NORMAL_COMPONENTS = 3;
			constexpr GLint COLOR_COMPONENTS = 4;
			constexpr GLuint POSITION_LOCATION = 0;
			constexpr GLuint UV_LOCATION = 1;
			constexpr GLuint NORMAL_LOCATION = 2;
			constexpr GLuint COLOR_LOCATION = 3;

			// A divisor no instance count reaches reads the first value throughout.
			constexpr GLuint PER_VERTEX = 0;
			constexpr GLuint PER_DRAW = std::numeric_limits<GLuint>::max();

			const VertexFormat format = layout.format;
			const GLsizei stride = (GLsizei)(FloatsPerVertex(layout) * sizeof(float));

			glUseProgram(program);
			BindTextures(program);

			glBindBuffer(GL_ARRAY_BUFFER, buffer);

			// Left enabled, since toggling per draw recompiles the vertex shader; missing ones read defaults.
			const GLsizeiptr uvOffset = format != VertexFormat::Position ? POSITION_COMPONENTS * sizeof(float) : 0;
			const GLsizeiptr normalOffset = (POSITION_COMPONENTS + UV_COMPONENTS) * sizeof(float);
			const GLsizeiptr colorOffset = normalOffset + NORMAL_COMPONENTS * sizeof(float);
			const bool hasNormals = format == VertexFormat::PositionUVNormal || format == VertexFormat::PositionUVNormalColor;
			const bool hasColors = format == VertexFormat::PositionUVNormalColor;

			glVertexAttribPointer(POSITION_LOCATION, POSITION_COMPONENTS, GL_FLOAT, GL_FALSE, stride, (void*)0);
			glEnableVertexAttribArray(POSITION_LOCATION);
			glVertexAttribPointer(UV_LOCATION, UV_COMPONENTS, GL_FLOAT, GL_FALSE, stride, (void*)uvOffset);
			glEnableVertexAttribArray(UV_LOCATION);

			if (hasNormals)
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

			// Read from buffer, still bound, since a format with colours has normals.
			if (hasColors)
			{
				glVertexAttribPointer(COLOR_LOCATION, COLOR_COMPONENTS, GL_FLOAT, GL_FALSE, stride, (void*)colorOffset);
				glVertexAttribDivisor(COLOR_LOCATION, PER_VERTEX);
			}
			else
			{
				glBindBuffer(GL_ARRAY_BUFFER, m_noColor);
				glVertexAttribPointer(COLOR_LOCATION, COLOR_COMPONENTS, GL_FLOAT, GL_FALSE, 0, (void*)0);
				glVertexAttribDivisor(COLOR_LOCATION, PER_DRAW);
			};

			glEnableVertexAttribArray(COLOR_LOCATION);

			// A slot the vertices lack reads m_noUV's zeros.
			const std::vector<UVBinding>& slots = m_programs[program].uvSlots;

			for (const UVBinding& binding : slots)
			{
				const UVSource source = SourceOf(layout, binding.slot);

				if (source.words)
				{
					glBindBuffer(GL_ARRAY_BUFFER, buffer);
					glVertexAttribIPointer(binding.location, (GLint)source.words, GL_UNSIGNED_INT, stride, (void*)(source.offset * sizeof(float)));
					glVertexAttribDivisor(binding.location, PER_VERTEX);
				}
				else
				{
					glBindBuffer(GL_ARRAY_BUFFER, m_noUV);
					glVertexAttribIPointer(binding.location, WordsPerUVLocation, GL_UNSIGNED_INT, 0, (void*)0);
					glVertexAttribDivisor(binding.location, PER_DRAW);
				};

				glEnableVertexAttribArray(binding.location);
			};

			if (!instances)
				glDrawArrays(primitive, 0, (GLsizei)count);
			else
				DrawInstanced(primitive, count, instances, instanceCount);

			// Off again, since the next program may read these as another type.
			for (const UVBinding& binding : slots)
				glDisableVertexAttribArray(binding.location);

			glBindBuffer(GL_ARRAY_BUFFER, 0);
		};

		// Draws with each instance's model matrix, colour and seed read from m_instances.
		void DrawInstanced(uint32_t primitive, size_t count, const float* instances, size_t instanceCount)
		{
			constexpr GLint COLUMN_COMPONENTS = 4;
			constexpr GLint COLOR_AND_SEED_COMPONENTS = 4;
			constexpr GLuint COLUMNS = 4;
			constexpr GLsizei STRIDE = (GLsizei)(FloatsPerInstance * sizeof(float));
			constexpr GLuint PER_INSTANCE = 1;

			glBindBuffer(GL_ARRAY_BUFFER, m_instances);
			glBufferData(GL_ARRAY_BUFFER, instanceCount * STRIDE, instances, GL_STREAM_DRAW);

			for (GLuint column = 0; column < COLUMNS; column++)
			{
				const GLuint location = InstanceModelLocation + column;
				glVertexAttribPointer(location, COLUMN_COMPONENTS, GL_FLOAT, GL_FALSE, STRIDE, (void*)(column * COLUMN_COMPONENTS * sizeof(float)));
				glVertexAttribDivisor(location, PER_INSTANCE);
				glEnableVertexAttribArray(location);
			};

			glVertexAttribPointer(InstanceColorLocation, COLOR_AND_SEED_COMPONENTS, GL_FLOAT, GL_FALSE, STRIDE, (void*)(COLUMNS * COLUMN_COMPONENTS * sizeof(float)));
			glVertexAttribDivisor(InstanceColorLocation, PER_INSTANCE);
			glEnableVertexAttribArray(InstanceColorLocation);

			glDrawArraysInstanced(primitive, 0, (GLsizei)count, (GLsizei)instanceCount);

			// Off again, since WebGL refuses a draw whose enabled arrays run short.
			for (GLuint column = 0; column < COLUMNS; column++)
				glDisableVertexAttribArray(InstanceModelLocation + column);

			glDisableVertexAttribArray(InstanceColorLocation);
		};

		#pragma endregion

		#pragma region Textures

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

		#pragma endregion

		#pragma region Targets

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

			// Without these a colourless framebuffer is incomplete.
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

			// A texture rather than a renderbuffer, so TargetDepth can hand it out.
			glGenTextures(1, &target.depth);
			glBindTexture(GL_TEXTURE_2D, target.depth);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
			SetDepthSampling();
			glBindTexture(GL_TEXTURE_2D, bound);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, target.depth, 0);

			return Complete(target, width, height, previous);
		};

		uint32_t TargetDepth(uint32_t target) override
		{
			const auto found = m_targets.find(target);

			return found == m_targets.end() || found->second.compare ? 0 : found->second.depth;
		};

		void DestroyTexture(uint32_t texture) override
		{
			Unbind(texture);

			const auto found = m_targets.find(texture);

			if (found == m_targets.end())
			{
				glDeleteTextures(1, &texture);
				return;
			};

			Unbind(found->second.depth);
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

		#pragma endregion

		#pragma region Render State

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

			// Unbound first: drivers validate a clear like a draw, against unset textures.
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

		#pragma endregion

	private:
		#pragma region Targets

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

		// GL names a texture anew once it is gone, so no program may still bind it.
		void Unbind(GLuint texture)
		{
			if (!texture)
				return;

			for (auto& [program, state] : m_programs)
				std::replace(state.textures.begin(), state.textures.end(), texture, 0u);
		};

		// Creating a texture binds it over what the program may be sampling.
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

		// Creating a target binds its framebuffer, so the previous one is rebound after.
		uint32_t Complete(Target& target, int width, int height, GLuint previous)
		{
			const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

			glBindFramebuffer(GL_FRAMEBUFFER, previous);

			if (!complete)
			{
				std::cerr << "OpenGL: a " << width << 'x' << height << " render target is incomplete" << std::endl;
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
			if (target.depth)		glDeleteTextures(1, &target.depth);
		};

		#pragma endregion

		#pragma region State

		GLFWwindow* m_window = nullptr;
		const char* m_glslVersion = nullptr;

		CaptureHandler m_capture;

		GLuint m_vbo = 0;
		GLuint m_vao = 0;

		// What DrawInstances hands its instances over in.
		GLuint m_instances = 0;

		// CreateVertices' handles by GL name: vertex count and layout.
		struct Kept
		{
			size_t count = 0;
			VertexLayout layout;
		};

		std::unordered_map<GLuint, Kept> m_kept;

		// A normal of zeros, what aNormal reads from vertices without normals.
		GLuint m_noNormal = 0;

		// White, what aColor reads from vertices without colours.
		GLuint m_noColor = 0;

		// Zeros, what a UV layer slot reads from vertices without its layer.
		GLuint m_noUV = 0;

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

		// Per program: uniform locations, sampler units, bound textures and UV layer reads.
		struct Program
		{
			Locations locations;
			Locations units;
			std::vector<GLuint> textures;
			std::vector<UVBinding> uvSlots;
		};

		std::unordered_map<uint32_t, Program> m_programs;

		#pragma endregion
	};

	std::unique_ptr<Renderer> CreateOpenGLRenderer()
	{
		return std::make_unique<OpenGLRenderer>();
	};
};
