#include "doctest.h"

#include "OpenGL.h"
#include "Renderer.h"
#include "UniformType.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>


// The web's backends run in the browser suite, which needs WebGPU.
#ifndef __EMSCRIPTEN__
namespace
{
	constexpr int WINDOW_SIZE = 64;
	constexpr int IMAGE_SIZE = 4;
	constexpr int CHANNELS = 3;
	constexpr int CHANNEL_TOLERANCE = 2;
	constexpr uint8_t FULL = 255;
	constexpr uint8_t HALF = 128;
	constexpr size_t FULLSCREEN_VERTICES = 3;
	constexpr uint32_t ADD = 7;
	constexpr float BLACK[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	constexpr float GREEN[4] = { 0.0f, 1.0f, 0.0f, 1.0f };

	const std::string DOUBLE =
		"layout(local_size_x = 4) in;\n"
		"layout(std430) buffer Values { uint values[]; };\n"
		"uniform uint u_add;\n"
		"void main() { uint i = gl_GlobalInvocationID.x; values[i] = values[i] * 2u + u_add; }\n";

	// Each texel of a 4x4 image gets its x and y over 3 in red and green.
	const std::string PAINT =
		"layout(local_size_x = 4, local_size_y = 4) in;\n"
		"layout(rgba8) writeonly uniform image2D u_image;\n"
		"void main() { ivec2 p = ivec2(gl_GlobalInvocationID.xy); imageStore(u_image, p, vec4(vec2(p) / 3.0, 0.5, 1.0)); }\n";

	const std::string FULLSCREEN =
		"out vec2 v_uv;\n"
		"void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); v_uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";

	const std::string SAMPLE =
		"in vec2 v_uv;\n"
		"out vec4 FragColor;\n"
		"uniform sampler2D u_texture;\n"
		"void main() { FragColor = texture(u_texture, v_uv); }\n";

	const std::string TINTED =
		"layout(std430) readonly buffer Tint { vec4 tint; };\n"
		"flat out vec4 v_tint;\n"
		"void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); v_tint = tint; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";

	const std::string WRITES_TINT =
		"layout(std430) buffer Tint { vec4 tint; };\n"
		"flat out vec4 v_tint;\n"
		"void main() { tint = vec4(1.0); v_tint = tint; gl_Position = vec4(0.0); }\n";

	const std::string FLAT =
		"flat in vec4 v_tint;\n"
		"out vec4 FragColor;\n"
		"void main() { FragColor = v_tint; }\n";

	const std::string BROKEN = "void main() { nothing; }\n";

	const std::vector<uint32_t> VALUES = { 1, 2, 3, 4 };

	std::vector<uint32_t> Doubled()
	{
		std::vector<uint32_t> doubled;

		for (const uint32_t value : VALUES)
			doubled.push_back(value * 2 + ADD);

		return doubled;
	};

	// A hidden window on a backend, as Engine::OpenWindow opens one. renderer
	// is null where the machine cannot run it.
	struct Gpu
	{
		GLFWwindow* window = nullptr;
		std::unique_ptr<Loom::Renderer> renderer;

		explicit Gpu(Loom::Backend backend)
		{
			glfwInit();
			glfwDefaultWindowHints();
			renderer = Loom::Renderer::Create(backend);
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			window = glfwCreateWindow(WINDOW_SIZE, WINDOW_SIZE, "", nullptr, nullptr);

			if (!window || !renderer->Attach(window) || !renderer->SupportsCompute())
				renderer.reset();
		};

		~Gpu()
		{
			renderer.reset();

			if (window)
				glfwDestroyWindow(window);

			glfwTerminate();
		};

		std::vector<uint32_t> Read(uint32_t buffer)
		{
			std::vector<uint32_t> values;
			renderer->BeginFrame();
			renderer->ReadBuffer(
				buffer,
				[&](const std::vector<uint8_t>& data)
				{
					values.resize(data.size() / sizeof(uint32_t));
					std::memcpy(values.data(), data.data(), data.size());
				});
			renderer->EndFrame();
			return values;
		};

		// The window's bottom-left pixel after the frame draws.
		template<typename Frame>
		std::vector<uint8_t> Capture(const Frame& frame, bool topRight = false)
		{
			std::vector<uint8_t> pixel;
			renderer->BeginFrame();
			renderer->Clear(BLACK, true);
			frame();
			renderer->Capture(
				[&](int width, int height, const std::vector<uint8_t>& rgb)
				{
					const size_t at = topRight ? ((size_t)(height - 1) * width + width - 1) * CHANNELS : 0;
					pixel.assign(rgb.begin() + at, rgb.begin() + at + CHANNELS);
				});
			renderer->EndFrame();
			return pixel;
		};
	};

	bool Near(const std::vector<uint8_t>& pixel, uint8_t r, uint8_t g, uint8_t b)
	{
		const uint8_t expected[CHANNELS] = { r, g, b };

		if (pixel.size() != CHANNELS)
			return false;

		for (int c = 0; c < CHANNELS; c++)
			if (std::abs((int)pixel[c] - (int)expected[c]) > CHANNEL_TOLERANCE)
				return false;

		return true;
	};

	// Runs check on each desktop backend the machine has, one after another,
	// so a check can compare them.
	template<typename Check>
	void OnEachBackend(const Check& check)
	{
		for (const Loom::Backend backend : { Loom::Backend::OpenGL, Loom::Backend::Vulkan })
		{
			const char* name = backend == Loom::Backend::OpenGL ? "OpenGL" : "Vulkan";
			INFO(name);
			Gpu gpu(backend);

			if (!gpu.renderer)
			{
				MESSAGE(name << " has no compute here");
				continue;
			};

			check(gpu);
		};
	};

	std::string ErrorOf(Loom::Renderer& renderer, const std::string& vertex, const std::string& fragment)
	{
		try
		{
			renderer.CreateProgram("test", vertex, fragment);
		}
		catch (const std::runtime_error& e)
		{
			return e.what();
		};

		return "";
	};

	std::string ComputeErrorOf(Loom::Renderer& renderer, const std::string& compute)
	{
		try
		{
			renderer.CreateComputeProgram("test", compute);
		}
		catch (const std::runtime_error& e)
		{
			return e.what();
		};

		return "";
	};
};


TEST_SUITE("Compute")
{
	TEST_CASE("a dispatch doubles a buffer and ReadBuffer hands it back at the frame's end")
	{
		OnEachBackend([](Gpu& gpu)
		{
			Loom::Renderer& renderer = *gpu.renderer;
			const uint32_t program = renderer.CreateComputeProgram("double", DOUBLE);
			const uint32_t buffer = renderer.CreateBuffer(VALUES.size() * sizeof(uint32_t));
			renderer.SetBufferData(buffer, VALUES.data(), VALUES.size() * sizeof(uint32_t));

			renderer.BeginFrame();
			renderer.SetBuffer(program, "Values", buffer);
			renderer.SetUniform(program, "u_add", Loom::UniformType::UInt, &ADD);
			renderer.Dispatch(program, 1, 1, 1);
			renderer.EndFrame();

			CHECK(gpu.Read(buffer) == Doubled());

			renderer.DestroyBuffer(buffer);
			renderer.DeleteProgram(program);
		});
	};

	TEST_CASE("a dispatch with its buffer unset does nothing until it is set")
	{
		OnEachBackend([](Gpu& gpu)
		{
			Loom::Renderer& renderer = *gpu.renderer;
			const uint32_t program = renderer.CreateComputeProgram("double", DOUBLE);
			const uint32_t buffer = renderer.CreateBuffer(VALUES.size() * sizeof(uint32_t));
			renderer.SetBufferData(buffer, VALUES.data(), VALUES.size() * sizeof(uint32_t));
			renderer.SetUniform(program, "u_add", Loom::UniformType::UInt, &ADD);

			renderer.BeginFrame();
			renderer.Dispatch(program, 1, 1, 1);
			renderer.EndFrame();
			CHECK(gpu.Read(buffer) == VALUES);

			const uint32_t gone = renderer.CreateBuffer(VALUES.size() * sizeof(uint32_t));
			renderer.SetBuffer(program, "Values", gone);
			renderer.DestroyBuffer(gone);

			renderer.BeginFrame();
			renderer.Dispatch(program, 1, 1, 1);
			renderer.EndFrame();
			CHECK(gpu.Read(buffer) == VALUES);

			renderer.SetBuffer(program, "Values", buffer);
			renderer.BeginFrame();
			renderer.Dispatch(program, 1, 1, 1);
			renderer.EndFrame();
			CHECK(gpu.Read(buffer) == Doubled());

			renderer.DestroyBuffer(buffer);
			renderer.DeleteProgram(program);
		});
	};

	TEST_CASE("a fresh buffer reads back as zeros")
	{
		OnEachBackend([](Gpu& gpu)
		{
			const uint32_t buffer = gpu.renderer->CreateBuffer(VALUES.size() * sizeof(uint32_t));
			CHECK(gpu.Read(buffer) == std::vector<uint32_t>(VALUES.size(), 0));
			gpu.renderer->DestroyBuffer(buffer);
			CHECK(gpu.renderer->CreateBuffer(0) == 0);
		});
	};

	TEST_CASE("what a compute program writes to an image a draw samples where imageStore put it")
	{
		OnEachBackend([](Gpu& gpu)
		{
			Loom::Renderer& renderer = *gpu.renderer;
			const uint32_t paint = renderer.CreateComputeProgram("paint", PAINT);
			const uint32_t show = renderer.CreateProgram("show", FULLSCREEN, SAMPLE);
			const uint32_t image = renderer.CreateStorageTexture(IMAGE_SIZE, IMAGE_SIZE);
			renderer.SetImage(paint, "u_image", image);
			renderer.SetTexture(show, "u_texture", image);

			const auto frame = [&]()
			{
				renderer.Dispatch(paint, 1, 1, 1);
				renderer.Draw(show, GL_TRIANGLES, nullptr, FULLSCREEN_VERTICES, GL_STREAM_DRAW);
			};

			CHECK(Near(gpu.Capture(frame), 0, 0, HALF));
			CHECK(Near(gpu.Capture(frame, true), FULL, FULL, HALF));

			renderer.DestroyTexture(image);
			renderer.DeleteProgram(show);
			renderer.DeleteProgram(paint);
		});
	};

	TEST_CASE("a vertex stage reads a readonly storage block, and draws nothing until it is set")
	{
		OnEachBackend([](Gpu& gpu)
		{
			Loom::Renderer& renderer = *gpu.renderer;
			const uint32_t program = renderer.CreateProgram("tinted", TINTED, FLAT);
			const uint32_t tint = renderer.CreateBuffer(sizeof(GREEN));
			renderer.SetBufferData(tint, GREEN, sizeof(GREEN));

			const auto frame = [&]()
			{
				renderer.Draw(program, GL_TRIANGLES, nullptr, FULLSCREEN_VERTICES, GL_STREAM_DRAW);
			};

			CHECK(Near(gpu.Capture(frame), 0, 0, 0));

			renderer.SetBuffer(program, "Tint", tint);
			CHECK(Near(gpu.Capture(frame), 0, FULL, 0));

			renderer.DestroyBuffer(tint);
			renderer.DeleteProgram(program);
		});
	};

	TEST_CASE("a dispatch of a draw program and a draw of a compute program do nothing")
	{
		OnEachBackend([](Gpu& gpu)
		{
			Loom::Renderer& renderer = *gpu.renderer;
			const uint32_t compute = renderer.CreateComputeProgram("paint", PAINT);
			const uint32_t draw = renderer.CreateProgram("tinted", TINTED, FLAT);
			const uint32_t tint = renderer.CreateBuffer(sizeof(GREEN));
			renderer.SetBufferData(tint, GREEN, sizeof(GREEN));
			renderer.SetBuffer(draw, "Tint", tint);

			const auto frame = [&]()
			{
				renderer.Dispatch(draw, 1, 1, 1);
				renderer.Draw(compute, GL_TRIANGLES, nullptr, FULLSCREEN_VERTICES, GL_STREAM_DRAW);
			};

			CHECK(Near(gpu.Capture(frame), 0, 0, 0));

			renderer.DestroyBuffer(tint);
			renderer.DeleteProgram(draw);
			renderer.DeleteProgram(compute);
		});
	};

	TEST_CASE("both backends refuse the same shaders with the same words")
	{
		std::vector<std::vector<std::string>> errors;

		OnEachBackend([&](Gpu& gpu)
		{
			Loom::Renderer& renderer = *gpu.renderer;
			const std::vector<std::string> found =
			{
				ErrorOf(renderer, WRITES_TINT, FLAT),
				ComputeErrorOf(renderer, BROKEN),
			};

			for (const std::string& error : found)
				CHECK_FALSE(error.empty());

			errors.push_back(found);
		});

		if (errors.size() == 2)
			CHECK(errors[0] == errors[1]);
	};
};
#endif
