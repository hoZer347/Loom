#include "doctest.h"

#include "Render Support.h"

#ifndef __EMSCRIPTEN__

#include "Camera.h"
#include "EditableMesh.h"
#include "GameObject.h"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "Scene.h"
#include "Shaders.h"
#include "Skin.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using LoomTests::Image;
using LoomTests::Pump;
using LoomTests::RenderFixture;


#pragma region Helpers

namespace
{
	constexpr float CLEAR[] = { 0.0f, 0.0f, 1.0f, 1.0f };
	const glm::u8vec3 CLEARED(0, 0, 255);
	const glm::u8vec3 RED(255, 0, 0);

	// Placed in clip space, whatever the camera, and red.
	const std::string CLIP_SPACE_RED =
		"// ===VERTEX===\n"
		"layout(location = 0) in vec3 aPos;\n"
		"void main() { gl_Position = vec4(aPos, 1.0); }\n"
		"// ===FRAGMENT===\n"
		"out vec4 FragColor;\n"
		"void main() { FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

	// Moved by its model matrix alone, and as red as its seed.
	const std::string SEED_AS_RED =
		"// ===VERTEX===\n"
		"layout(location = 0) in vec3 aPos;\n"
		"uniform mat4 u_model;\n"
		"void main() { gl_Position = u_model * vec4(aPos, 1.0); }\n"
		"// ===FRAGMENT===\n"
		"uniform float u_seed;\n"
		"out vec4 FragColor;\n"
		"void main() { FragColor = vec4(u_seed, 0.0, 0.0, 1.0); }\n";

	// Half of clip space wide, centred on its origin.
	const std::vector<float> CENTRED_HALF
	{
		-0.5f, -1.0f, 0.0f,  0.5f, -1.0f, 0.0f,  0.5f, 1.0f, 0.0f,
		-0.5f, -1.0f, 0.0f,  0.5f,  1.0f, 0.0f, -0.5f, 1.0f, 0.0f,
	};

	// The right half of clip space.
	const std::vector<float> RIGHT_HALF
	{
		0.0f, -1.0f, 0.0f,  1.0f, -1.0f, 0.0f,  1.0f, 1.0f, 0.0f,
		0.0f, -1.0f, 0.0f,  1.0f,  1.0f, 0.0f,  0.0f, 1.0f, 0.0f,
	};

	// Puts Engine::clearColor back once a case that sets it is done.
	struct ClearedTo
	{
		explicit ClearedTo(const float* colour)
		{
			std::copy_n(Loom::Engine::clearColor, std::size(m_previous), m_previous);
			std::copy_n(colour, std::size(m_previous), Loom::Engine::clearColor);
		};

		~ClearedTo() { std::copy_n(m_previous, std::size(m_previous), Loom::Engine::clearColor); };

	private:
		float m_previous[std::size(Loom::Engine::clearColor)];
	};

	// Coloured by three UV layers the mesh holds, one of them past four
	// words, and one it lacks, which reads zeros.
	const std::string CLIP_SPACE_LAYERED =
		"// ===VERTEX===\n"
		"layout(location = 0) in vec3 aPos;\n"
		"UV_1<float red, bool lit>;\n"
		"UV_2<vec4 skipped, float blue>;\n"
		"UV_3<float>;\n"
		"out vec4 v_color;\n"
		"void main() { v_color = vec4(UV_1.red - UV_3, UV_1.lit ? 1.0 : 0.0, UV_2.blue, 1.0); gl_Position = vec4(aPos, 1.0); }\n"
		"// ===FRAGMENT===\n"
		"in vec4 v_color;\n"
		"out vec4 FragColor;\n"
		"void main() { FragColor = v_color; }\n";

	const glm::u8vec3 WHITE(255, 255, 255);

	struct VulkanRenderFixture : RenderFixture
	{
		VulkanRenderFixture() : RenderFixture(DEFAULT_SIZE, Loom::Backend::Vulkan) { };
	};

	// The right half drawn with CLIP_SPACE_LAYERED, its layers making it white.
	Image LayeredFrame(RenderFixture& fixture, const std::string& name)
	{
		const ClearedTo cleared(CLEAR);

		std::istringstream source(CLIP_SPACE_LAYERED);
		Loom::Shader shader(name, source);

		Loom::Scene scene(name);
		Pump();

		scene.Attach<Loom::Camera>();
		Loom::GameObject* half = scene.AddChild("Right Half");
		Pump();

		half->Attach<Loom::Material>()->shader = &shader;

		Loom::Mesh* mesh = half->Attach<Loom::Mesh>();
		constexpr size_t POSITION_COMPONENTS = 3;
		const size_t vertices = RIGHT_HALF.size() / POSITION_COMPONENTS;

		mesh->m_vertices = RIGHT_HALF;
		mesh->SetUV<float, bool>(1, std::vector<std::tuple<float, bool>>(vertices, { 1.0f, true }));
		mesh->SetUV<glm::vec4, float>(2, std::vector<std::tuple<glm::vec4, float>>(vertices, { glm::vec4(0.5f), 1.0f }));
		Pump();

		const Image image = fixture.Frame();

		image.Save(name);

		return image;
	};

	// The right half, bound to a joint the GPU moves left by a whole half.
	Image SkinnedFrame(RenderFixture& fixture, const std::string& name)
	{
		const ClearedTo cleared(CLEAR);

		std::istringstream source(CLIP_SPACE_RED);
		Loom::Shader shader(name, source);

		REQUIRE(shader.skinned_id != 0);

		Loom::Scene scene(name);
		Pump();

		scene.Attach<Loom::Camera>();
		Loom::GameObject* bone = scene.AddChild("Bone");
		Loom::GameObject* half = scene.AddChild("Right Half");
		Pump();

		half->Attach<Loom::Material>()->shader = &shader;

		Loom::Mesh* mesh = half->Attach<Loom::Mesh>();
		Loom::Skin* skin = half->Attach<Loom::Skin>();
		Pump();

		const glm::mat4 identity(1.0f);

		mesh->m_vertices = RIGHT_HALF;
		skin->skeleton = &scene.GetRoot();
		skin->m_joints = "Bone\n";
		skin->m_inverseBinds = std::vector<float>(glm::value_ptr(identity), glm::value_ptr(identity) + Loom::Skin::MATRIX_FLOATS);
		skin->m_influences = LoomTests::Bound({ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
		bone->transform.position = glm::vec3(-1.0f, 0.0f, 0.0f);
		Pump();

		REQUIRE(mesh->ProgramFor(shader.id, shader.skinned_id) == shader.skinned_id);

		const Image image = fixture.Frame();

		image.Save(name);

		return image;
	};

	// The starter scene's shader: white, lit and shadowed.
	const std::string LIT =
		"// ===SETTINGS===\n"
		"shading = on\n"
		"shadows = on\n"
		"// ===VERTEX===\n"
		"layout(location = 0) in vec3 aPos;\n"
		"uniform mat4 u_model;\n"
		"uniform mat4 u_viewProjection;\n"
		"void main() { gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0); }\n"
		"// ===FRAGMENT===\n"
		"uniform vec3 u_color;\n"
		"out vec4 FragColor;\n"
		"void main() { FragColor = vec4(u_color, 1.0); }\n";

	// Big enough that a shadow map texel spans a few pixels at the contact.
	constexpr glm::ivec2 CONTACT_SIZE{ 640, 360 };

	// The starter scene's plane and sphere, the sphere at the origin half
	// through the plane, under the default light.
	constexpr float CONTACT_PLANE_SIZE = 4.0f;
	constexpr float CONTACT_RADIUS = 0.5f;
	constexpr int CONTACT_SEGMENTS = 16;
	constexpr int CONTACT_RINGS = 8;

	// Where a world point lands in a frame the camera drew, from its top left.
	glm::ivec2 PixelOf(const Loom::Camera& camera, const Image& image, const glm::vec3& world)
	{
		const glm::vec4 clip = camera.ViewProjection((float)image.width / image.height) * glm::vec4(world, 1.0f);
		const glm::vec2 ndc = glm::vec2(clip) / clip.w;

		return glm::ivec2((ndc.x + 1.0f) * 0.5f * image.width, (1.0f - ndc.y) * 0.5f * image.height);
	};

	// The sphere half through the plane, seen by a camera at position turned
	// by rotation, for as long as it lives, with or without mesh normals.
	struct Contact
	{
		Contact(const std::string& name, const glm::vec3& position, const glm::vec3& rotation, bool normals) :
			m_source(LIT),
			m_shader(name, m_source),
			m_scene(name)
		{
			Pump();

			light = m_scene.Attach<Loom::Light>();
			Loom::GameObject* eye = m_scene.AddChild("Camera");
			Loom::GameObject* plane = m_scene.AddChild("Plane");
			Loom::GameObject* sphere = m_scene.AddChild("Sphere");
			Pump();

			camera = eye->Attach<Loom::Camera>();
			eye->transform.position = position;
			eye->transform.rotation = rotation;

			plane->Attach<Loom::Material>()->shader = &m_shader;
			Loom::EditableMesh::Plane(CONTACT_PLANE_SIZE).WriteTo(*plane->Attach<Loom::Mesh>());

			sphere->Attach<Loom::Material>()->shader = &m_shader;
			Loom::EditableMesh::Sphere(CONTACT_RADIUS, CONTACT_SEGMENTS, CONTACT_RINGS).WriteTo(*sphere->Attach<Loom::Mesh>());

			if (!normals)
				for (Loom::GameObject* shape : { plane, sphere })
					shape->GetComponent<Loom::Mesh>()->m_normals = std::vector<float>();

			Pump();
		};

		Loom::Light* light = nullptr;
		Loom::Camera* camera = nullptr;

	private:
		std::istringstream m_source;
		Loom::Shader m_shader;
		Loom::Scene m_scene;
	};

	struct ContactFixture : RenderFixture
	{
		ContactFixture() : RenderFixture(CONTACT_SIZE) { };
	};

	struct VulkanContactFixture : RenderFixture
	{
		VulkanContactFixture() : RenderFixture(CONTACT_SIZE, Loom::Backend::Vulkan) { };
	};

	// The largest difference in red between the two frames at the points.
	int Furthest(const Loom::Camera& camera, const Image& shadowed, const Image& unshadowed, const std::vector<glm::vec3>& points)
	{
		int furthest = 0;

		for (const glm::vec3& point : points)
		{
			const glm::ivec2 pixel = PixelOf(camera, shadowed, point);
			furthest = std::max(furthest, std::abs(shadowed.At(pixel.x, pixel.y).r - unshadowed.At(pixel.x, pixel.y).r));
		};

		return furthest;
	};

	// Seen from under the plane, the sphere is in the plane's shadow right up
	// to it; seen from above, what nothing stands between and the light is lit
	// just as with no shadows at all, with no acne.
	void CheckContact(RenderFixture& fixture, const std::string& backend)
	{
		const ClearedTo cleared(CLEAR);

		constexpr glm::vec3 UNDER_POSITION(0.0f, -0.4f, 2.0f);
		constexpr glm::vec3 UNDER_ROTATION(12.0f, 0.0f, 0.0f);
		constexpr glm::vec3 ABOVE_POSITION(0.0f, 2.0f, 2.0f);
		constexpr glm::vec3 ABOVE_ROTATION(-45.0f, 0.0f, 0.0f);

		// The sphere is read down to the PCF kernel's reach below the plane.
		constexpr float TEXELS_BELOW = 2.0f;
		constexpr int DOWN_STEPS = 8;
		constexpr float ACROSS_EXTENTS = 2.0f;

		// Off rounding, and a pixel landing a little off the point.
		constexpr int TOLERANCE = 8;

		// Sphere points lit at least this much and facing the camera at least
		// this much, so the light grazes some and none sits on the silhouette.
		constexpr float LIT_AT_LEAST = 0.3f;
		constexpr float FACING_AT_LEAST = 0.4f;
		constexpr int STEPS = 24;
		constexpr float TURN = glm::two_pi<float>();
		constexpr float HALF_TURN = glm::pi<float>();

		// The default light, whose frames are saved, then two lower suns past the pull's slope limit.
		const glm::vec3 LIGHTS[] = { Loom::Light().direction, { -0.8f, -1.0f, -0.45f }, { -1.0f, -0.5f, -0.3f } };

		for (const glm::vec3& direction : LIGHTS)
			for (bool normals : { true, false })
			{
				INFO("light direction ", direction.x, ", ", direction.y, ", ", direction.z, std::string(normals ? " with normals" : " without normals"));
				const bool saved = &direction == LIGHTS && normals;

				{
					Contact contact("Render - shadow at a contact from under, on " + backend, UNDER_POSITION, UNDER_ROTATION, normals);
					contact.light->direction = direction;
					const Image image = fixture.Frame();

					if (saved)
						image.Save("Render - shadow at a contact from under, on " + backend);

					const glm::vec3 toLight = -glm::normalize((glm::vec3)contact.light->direction);
					const float texel = ACROSS_EXTENTS * contact.light->shadowExtent / contact.light->shadowResolution;
					int brightest = 0;

					// Along the crease where the sphere faces both the camera and the light.
					for (int around = 0; around < STEPS; around++)
						for (int down = 0; down <= DOWN_STEPS; down++)
						{
							const float azimuth = TURN * around / STEPS;
							const glm::vec3 outward(std::cos(azimuth), 0.0f, std::sin(azimuth));
							const float below = TEXELS_BELOW * texel * down / DOWN_STEPS;
							const glm::vec3 point = outward * std::sqrt(CONTACT_RADIUS * CONTACT_RADIUS - below * below) - glm::vec3(0.0f, below, 0.0f);

							if (glm::dot(outward, toLight) >= LIT_AT_LEAST && glm::dot(outward, glm::normalize(UNDER_POSITION - point)) >= FACING_AT_LEAST)
							{
								const glm::ivec2 pixel = PixelOf(*contact.camera, image, point);
								brightest = std::max(brightest, (int)image.At(pixel.x, pixel.y).r);
							};
						};

					CHECK(brightest <= (int)(contact.light->ambient * UINT8_MAX) + TOLERANCE);
				};

				{
					Contact contact("Render - shadow at a contact from above, on " + backend, ABOVE_POSITION, ABOVE_ROTATION, normals);

					contact.light->direction = direction;
					// No ambient, so nothing lit reaches white and hides acne.
					contact.light->ambient = 0.0f;

					const Image shadowed = fixture.Frame();

					if (saved)
						shadowed.Save("Render - shadow at a contact from above, on " + backend);

					contact.light->castShadows = false;
					const Image unshadowed = fixture.Frame();

					const glm::vec3 toLight = -glm::normalize((glm::vec3)contact.light->direction);
					std::vector<glm::vec3> lit;

					for (int around = 0; around < STEPS; around++)
						for (int up = 1; up < STEPS / 2; up++)
						{
							const float azimuth = TURN * around / STEPS;
							const float polar = HALF_TURN * up / STEPS;
							const glm::vec3 normal(std::sin(polar) * std::cos(azimuth), std::cos(polar), std::sin(polar) * std::sin(azimuth));
							const glm::vec3 point = normal * CONTACT_RADIUS;

							if (glm::dot(normal, toLight) >= LIT_AT_LEAST && glm::dot(normal, glm::normalize(ABOVE_POSITION - point)) >= FACING_AT_LEAST)
								lit.push_back(point);
						};

					// Well clear of the sphere's shadow, which falls the light's way.
					lit.push_back(glm::vec3(1.0f, 0.0f, 1.0f));

					CHECK(Furthest(*contact.camera, shadowed, unshadowed, lit) <= TOLERANCE);
				};
			};
	};

	uint32_t BigEndian(const std::vector<char>& bytes, size_t at)
	{
		constexpr int BYTE = 8;
		uint32_t value = 0;

		for (size_t i = at; i < at + sizeof(uint32_t); i++)
			value = (value << BYTE) | (uint8_t)bytes[i];

		return value;
	};
};

#pragma endregion


TEST_SUITE("Render")
{
	#pragma region Frames

	TEST_CASE_FIXTURE(RenderFixture, "a frame reads back the size the window was opened, top row first, in the colour it was cleared to")
	{
		const ClearedTo cleared(CLEAR);

		const Image image = Draw([]() { });

		CHECK(image.width == DEFAULT_SIZE.x);
		CHECK(image.height == DEFAULT_SIZE.y);
		CHECK(image.At(0, 0) == CLEARED);
		CHECK(image.At(image.width - 1, image.height - 1) == CLEARED);
	};

	TEST_CASE_FIXTURE(RenderFixture, "a whole frame draws the scenes as a game does")
	{
		const ClearedTo cleared(CLEAR);

		std::istringstream source(CLIP_SPACE_RED);
		Loom::Shader shader("render right half", source);

		Loom::Scene scene("render right half");
		Pump();

		scene.Attach<Loom::Camera>();
		Loom::GameObject* half = scene.AddChild("Right Half");
		Pump();

		half->Attach<Loom::Material>()->shader = &shader;
		half->Attach<Loom::Mesh>()->m_vertices = RIGHT_HALF;
		Pump();

		const Image image = Frame();
		const int middle = image.height / 2;

		image.Save("Render - right half");

		CHECK(image.At(0, middle) == CLEARED);
		CHECK(image.At(image.width / 4, middle) == CLEARED);
		CHECK(image.At(image.width * 3 / 4, middle) == RED);
		CHECK(image.At(image.width - 1, middle) == RED);
	};

	TEST_CASE_FIXTURE(RenderFixture, "alike meshes drawn as instances each read their own seed, the one they read when drawn alone")
	{
		const ClearedTo cleared(CLEAR);

		std::istringstream source(SEED_AS_RED);
		Loom::Shader shader("render seeds", source);
		REQUIRE(shader.instanced_id != 0);

		Loom::Scene scene("render seeds");
		Pump();

		scene.Attach<Loom::Camera>();

		constexpr float HALF_OFFSET = 0.5f;
		Loom::GameObject* left = scene.AddChild("Left");
		Loom::GameObject* right = scene.AddChild("Right");
		Pump();

		left->transform.position = glm::vec3(-HALF_OFFSET, 0.0f, 0.0f);
		right->transform.position = glm::vec3(HALF_OFFSET, 0.0f, 0.0f);

		for (Loom::GameObject* half : { left, right })
		{
			half->Attach<Loom::Material>()->shader = &shader;
			half->Attach<Loom::Mesh>()->m_vertices = CENTRED_HALF;
		};

		const Loom::Guid first(1, 2);
		left->SetGuid(first);
		right->SetGuid(Loom::Guid(3, 4));
		Pump();

		const auto reds = [this]()
		{
			const Image image = Frame();
			const int middle = image.height / 2;

			return std::pair(image.At(image.width / 4, middle).r, image.At(image.width * 3 / 4, middle).r);
		};

		const auto [instanced, other] = reds();
		CHECK(instanced != other);

		right->SetGuid(first);
		CHECK(reds().second == instanced);

		right->GetComponent<Loom::Mesh>()->m_draw_type = Loom::Mesh::Dynamic;
		CHECK(reds() == std::pair(instanced, instanced));
	};

	TEST_CASE_FIXTURE(RenderFixture, "a screenshot is a PNG of the frame in the suite's screenshot folder")
	{
		// Where a PNG's width and height are, after its signature and the
		// first chunk's length and type.
		constexpr size_t WIDTH_AT = 16;
		constexpr size_t HEIGHT_AT = 20;
		const std::string SIGNATURE = "\x89PNG\r\n\x1a\n";

		const Image image = Draw([]() { });
		const std::filesystem::path path = image.Save("Render - screenshot");

		CHECK(path.parent_path() == LoomTests::ScreenshotFolder());
		REQUIRE(std::filesystem::exists(path));

		std::ifstream file(path, std::ios::binary);
		const std::vector<char> bytes{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

		REQUIRE(bytes.size() > HEIGHT_AT + sizeof(uint32_t));
		CHECK(std::string(bytes.begin(), bytes.begin() + SIGNATURE.size()) == SIGNATURE);
		CHECK(BigEndian(bytes, WIDTH_AT) == (uint32_t)image.width);
		CHECK(BigEndian(bytes, HEIGHT_AT) == (uint32_t)image.height);
	};

	#pragma endregion

	#pragma region UV Layers

	TEST_CASE_FIXTURE(RenderFixture, "a vertex stage reads a mesh's UV layers as the types it declares, on OpenGL")
	{
		const Image image = LayeredFrame(*this, "Render - UV layers on OpenGL");
		const int middle = image.height / 2;

		CHECK(image.At(image.width / 4, middle) == CLEARED);
		CHECK(image.At(image.width * 3 / 4, middle) == WHITE);
	};

	TEST_CASE_FIXTURE(VulkanRenderFixture, "a vertex stage reads a mesh's UV layers as the types it declares, on Vulkan")
	{
		REQUIRE(Loom::Engine::backend == Loom::Backend::Vulkan);

		const Image image = LayeredFrame(*this, "Render - UV layers on Vulkan");
		const int middle = image.height / 2;

		CHECK(image.At(image.width / 4, middle) == CLEARED);
		CHECK(image.At(image.width * 3 / 4, middle) == WHITE);
	};

	#pragma endregion

	#pragma region Shadows

	TEST_CASE_FIXTURE(ContactFixture, "a shadow reaches right up to the caster a surface pokes through, with no acne on the open caster, on OpenGL")
	{
		CheckContact(*this, "OpenGL");
	};

	TEST_CASE_FIXTURE(VulkanContactFixture, "a shadow reaches right up to the caster a surface pokes through, with no acne on the open caster, on Vulkan")
	{
		REQUIRE(Loom::Engine::backend == Loom::Backend::Vulkan);

		CheckContact(*this, "Vulkan");
	};

	#pragma endregion

	#pragma region Skinning

	TEST_CASE_FIXTURE(RenderFixture, "a skinned mesh is bent by its joints on the GPU, on OpenGL")
	{
		const Image image = SkinnedFrame(*this, "Render - GPU skinning on OpenGL");
		const int middle = image.height / 2;

		CHECK(image.At(image.width / 4, middle) == RED);
		CHECK(image.At(image.width * 3 / 4, middle) == CLEARED);
	};

	TEST_CASE_FIXTURE(VulkanRenderFixture, "a skinned mesh is bent by its joints on the GPU, on Vulkan")
	{
		REQUIRE(Loom::Engine::backend == Loom::Backend::Vulkan);

		const Image image = SkinnedFrame(*this, "Render - GPU skinning on Vulkan");
		const int middle = image.height / 2;

		CHECK(image.At(image.width / 4, middle) == RED);
		CHECK(image.At(image.width * 3 / 4, middle) == CLEARED);
	};

	#pragma endregion
};

#endif
