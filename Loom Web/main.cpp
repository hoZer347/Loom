// Loom Web
// ---------
// The scene the portfolio site embeds on its Engine tab. It is a normal Loom
// scene: an Engine, a Scene, and GameObjects carrying Components. Nothing here
// is web-specific except the entry point, which hands the loop to Emscripten.
//
// The WASM build of Loom has no transform hierarchy and no uniform plumbing on
// Material, so the model/view/projection work happens on the CPU inside
// WireShape::OnUpdate, which writes normalized-device coordinates straight into
// its sibling Mesh. Engine::renderFrame runs every Update before every Render,
// so the geometry a component writes is always the geometry that frame draws.

#include "OpenGL.h"

#include "Engine.h"
#include "Scene.h"
#include "GameObject.h"
#include "Component.h"
#include "Shaders.h"
#include "Material.h"
#include "Mesh.h"
#include "Input.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <cmath>
#include <vector>

using namespace Loom;
using namespace glm;


// The shader every object in the scene shares. Fetched over HTTP by
// Shader::Request, so the path is relative to the page that loads index.js.
static const char* SHADER_FILE = "Loom.shader";

// The camera. Fixed: the scene turns, the viewer does not.
static const vec3 EYE = vec3(0.0f, 2.6f, 8.0f);
static const vec3 TARGET = vec3(0.0f, 0.0f, 0.0f);

// The depth range. The fragment shader needs both to turn the nonlinear depth
// it receives back into a distance it can shade on, so they live here rather
// than being buried in the perspective call.
static const float NEAR_PLANE = 0.1f;
static const float FAR_PLANE = 100.0f;


// --- Frame state -----------------------------------------------------------

// Seconds since the first frame, plus the projection matrix for the current
// canvas size. One component owns this so every WireShape agrees on the frame.
struct FrameState final :
	public Component<FrameState>
{
	static inline float time = 0.0f;
	static inline mat4 viewProjection = mat4(1.0f);

	void OnUpdate() override
	{
		const double now = glfwGetTime();
		time = static_cast<float>(now);

		// Ask the canvas its size rather than reading Input::screen_*, which is
		// only populated once a resize or mouse-move callback has fired. On the
		// very first frames neither has, and the scene would render to a
		// guessed aspect ratio until the user moved the mouse.
		int pixel_width = 0, pixel_height = 0;
#if __EMSCRIPTEN__
		emscripten_get_canvas_element_size("#canvas", &pixel_width, &pixel_height);
#else
		glfwGetWindowSize(Engine::window, &pixel_width, &pixel_height);
#endif

		const float w = pixel_width > 0 ? (float)pixel_width : 1600.0f;
		const float h = pixel_height > 0 ? (float)pixel_height : 900.0f;

		// Own the viewport rather than waiting to be told about it. The engine
		// updates it from a window resize callback, which never fires when the
		// canvas is already the size it wants to be, leaving the viewport at the
		// dimensions glfwCreateWindow asked for and the scene drawn into a
		// corner of the canvas.
		if (pixel_width > 0 && pixel_height > 0)
			glViewport(0, 0, pixel_width, pixel_height);

		// Lean the camera toward the cursor, a little, so the scene answers the
		// mouse without the viewer having to drag anything. Input::mouse_* is
		// in CSS pixels over the whole page, which for a full-bleed canvas is
		// the canvas.
		const float css_w = Input::screen_width > 0 ? (float)Input::screen_width : w;
		const float css_h = Input::screen_height > 0 ? (float)Input::screen_height : h;

		// (0, 0) is also the value Input starts at, so treat it as "the pointer
		// has not been anywhere yet" and stay centred rather than leaning into
		// the top-left corner before the visitor has touched anything.
		const bool pointer_seen = Input::mouse_x != 0 || Input::mouse_y != 0;

		const vec2 cursor = pointer_seen
			? vec2(
				Input::mouse_x / css_w * 2.0f - 1.0f,
				Input::mouse_y / css_h * 2.0f - 1.0f)
			: vec2(0.0f);

		// Ease toward the target rather than snapping, so a fast mouse does not
		// jerk the camera.
		m_lean += (clamp(cursor, vec2(-1.0f), vec2(1.0f)) - m_lean) * 0.06f;

		const vec3 eye = EYE + vec3(m_lean.x * 1.4f, -m_lean.y * 0.9f, 0.0f);

		const mat4 projection = perspective(radians(45.0f), w / h, NEAR_PLANE, FAR_PLANE);
		const mat4 view = lookAt(eye, TARGET, vec3(0.0f, 1.0f, 0.0f));

		viewProjection = projection * view;
	};

	// Runs before any Mesh in the scene, so the program is bound and its
	// uniforms are set by the time the meshes draw with it.
	void OnRender() override
	{
		if (shader == nullptr)
			return;

		glUseProgram(shader->id);

		SetFloat("u_time", time);
		SetFloat("u_near", NEAR_PLANE);
		SetFloat("u_far", FAR_PLANE);
	};

	Shader* shader = nullptr;

private:
	void SetFloat(const char* name, float value) const
	{
		const GLint location = glGetUniformLocation(shader->id, name);
		if (location >= 0)
			glUniform1f(location, value);
	};

	vec2 m_lean = vec2(0.0f);
};


// --- Geometry --------------------------------------------------------------

// Model-space line lists. Each pair of points is one GL_LINES segment.

static std::vector<vec3> CubeEdges()
{
	static const vec3 corner[8] = {
		{ -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
		{ -1, -1,  1 }, { 1, -1,  1 }, { 1, 1,  1 }, { -1, 1,  1 },
	};
	static const int edge[12][2] = {
		{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, // back face
		{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }, // front face
		{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, // the struts between them
	};

	std::vector<vec3> lines;
	for (const auto& e : edge)
	{
		lines.push_back(corner[e[0]]);
		lines.push_back(corner[e[1]]);
	};

	return lines;
};

// A unit icosahedron, built from the three golden-ratio rectangles. Every pair
// of vertices closer than a threshold is an edge, which is exact here because
// an icosahedron's edges are all the same length and are strictly shorter than
// any non-edge chord.
static std::vector<vec3> IcosahedronEdges()
{
	const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;

	std::vector<vec3> vertex;
	for (float s1 : { -1.0f, 1.0f })
		for (float s2 : { -1.0f, 1.0f })
		{
			vertex.push_back(normalize(vec3(0.0f, s1 * 1.0f, s2 * t)));
			vertex.push_back(normalize(vec3(s1 * 1.0f, s2 * t, 0.0f)));
			vertex.push_back(normalize(vec3(s1 * t, 0.0f, s2 * 1.0f)));
		};

	// Edge length of a unit icosahedron is 2/sqrt(phi*sqrt(5)) ~= 1.0515; the
	// next chord up is ~1.70, so anything under 1.2 is an edge and nothing else.
	const float edgeLength = 1.2f;

	std::vector<vec3> lines;
	for (size_t i = 0; i < vertex.size(); i++)
		for (size_t j = i + 1; j < vertex.size(); j++)
			if (distance(vertex[i], vertex[j]) < edgeLength)
			{
				lines.push_back(vertex[i]);
				lines.push_back(vertex[j]);
			};

	return lines;
};

// A flat grid on the XZ plane, for a floor the spinning shapes read against.
static std::vector<vec3> GridLines(int half, float step)
{
	const float extent = half * step;

	std::vector<vec3> lines;
	for (int i = -half; i <= half; i++)
	{
		const float d = i * step;

		lines.push_back(vec3(d, 0.0f, -extent));
		lines.push_back(vec3(d, 0.0f, extent));

		lines.push_back(vec3(-extent, 0.0f, d));
		lines.push_back(vec3(extent, 0.0f, d));
	};

	return lines;
};


// --- The animating component ----------------------------------------------

// Holds a shape in model space and, each frame, spins it, orbits it, projects
// it, and writes the result into the Mesh sitting on the same GameObject.
struct WireShape final :
	public Component<WireShape>
{
	WireShape(std::vector<vec3> model) :
		m_model(std::move(model))
	{ };

	vec3 orbitAxis = vec3(0.0f, 1.0f, 0.0f);
	float orbitRadius = 0.0f;
	float orbitSpeed = 0.0f;
	float orbitPhase = 0.0f;

	vec3 spinAxis = vec3(0.0f, 1.0f, 0.0f);
	float spinSpeed = 0.0f;

	vec3 origin = vec3(0.0f);
	float scale = 1.0f;
	float bobHeight = 0.0f;
	float bobSpeed = 0.0f;

	void OnUpdate() override
	{
		if (m_mesh == nullptr)
		{
			m_mesh = m_gameObject->GetComponent<Mesh>();
			if (m_mesh == nullptr)
				return;
		};

		const float t = FrameState::time;

		// Where the shape sits this frame: its origin, plus a circular orbit
		// about orbitAxis, plus a vertical bob.
		const float angle = t * orbitSpeed + orbitPhase;
		const vec3 a = normalize(orbitAxis);
		// Any vector not parallel to the axis gives a basis for the orbit plane.
		const vec3 seed = std::abs(a.y) < 0.9f ? vec3(0.0f, 1.0f, 0.0f) : vec3(1.0f, 0.0f, 0.0f);
		const vec3 u = normalize(cross(a, seed));
		const vec3 v = cross(a, u);

		vec3 position = origin + (u * std::cos(angle) + v * std::sin(angle)) * orbitRadius;
		position.y += std::sin(t * bobSpeed + orbitPhase) * bobHeight;

		mat4 model = translate(mat4(1.0f), position);
		if (spinSpeed != 0.0f)
			model = rotate(model, t * spinSpeed, normalize(spinAxis));
		model = glm::scale(model, vec3(scale));

		const mat4 mvp = FrameState::viewProjection * model;

		// Project on the CPU and hand the Mesh clip-space positions with w == 1,
		// which the vertex shader passes through untouched.
		m_vertices.clear();
		m_vertices.reserve(m_model.size() * 3);

		for (const vec3& point : m_model)
		{
			const vec4 clip = mvp * vec4(point, 1.0f);

			// Behind the eye: collapse the segment rather than let the divide
			// mirror it across the screen.
			if (clip.w <= 0.0001f)
			{
				m_vertices.insert(m_vertices.end(), { 0.0f, 0.0f, 2.0f });
				continue;
			};

			const vec3 ndc = vec3(clip) / clip.w;
			m_vertices.insert(m_vertices.end(), { ndc.x, ndc.y, ndc.z });
		};

		m_mesh->m_vertices = m_vertices;
	};

private:
	std::vector<vec3> m_model;
	std::vector<float> m_vertices;
	Mesh* m_mesh = nullptr;
};


// --- Scene assembly --------------------------------------------------------

// Attach order matters: WireShape writes the geometry the Mesh then draws, and
// components render in the order they were attached.
static GameObject* AddWireShape(
	Scene& scene,
	const std::string& name,
	std::vector<vec3> model,
	Shader* shader,
	WireShape** out)
{
	GameObject* object = scene.AddChild(name);

	*out = object->Attach<WireShape>(std::move(model));

	Material* material = object->Attach<Material>();
	material->shader = shader;

	Mesh* mesh = object->Attach<Mesh>(Mesh::Lines);
	mesh->m_draw_type = Mesh::Dynamic; // rewritten every frame
	mesh->material = material;

	return object;
};


int main()
{
	Engine engine;

	// slate-950, the colour the portfolio page behind the canvas is painted.
	Engine::clearColor[0] = 0.008f;
	Engine::clearColor[1] = 0.023f;
	Engine::clearColor[2] = 0.090f;
	Engine::clearColor[3] = 1.0f;

	// The GL context exists now, so the shader can compile. Under Emscripten
	// this fetches over HTTP and unwinds on ASYNCIFY until the bytes arrive.
	Shader* shader = new Shader(SHADER_FILE);

	Scene scene{ "Loom Web" };

	// Binds the shared program and its per-frame uniforms. Attached to the
	// scene root, which renders before any child, so it runs first.
	FrameState* frame = scene.Attach<FrameState>();
	frame->shader = shader;

	WireShape* shape = nullptr;

	AddWireShape(scene, "Floor", GridLines(8, 0.5f), shader, &shape);
	shape->origin = vec3(0.0f, -1.6f, 0.0f);
	shape->spinAxis = vec3(0.0f, 1.0f, 0.0f);
	shape->spinSpeed = 0.05f;

	AddWireShape(scene, "Core", IcosahedronEdges(), shader, &shape);
	shape->scale = 1.15f;
	shape->spinAxis = vec3(0.3f, 1.0f, 0.15f);
	shape->spinSpeed = 0.45f;
	shape->bobHeight = 0.12f;
	shape->bobSpeed = 0.9f;

	// Three cubes on a shared orbit, evenly spaced around it.
	for (int i = 0; i < 3; i++)
	{
		AddWireShape(scene, "Satellite", CubeEdges(), shader, &shape);

		shape->scale = 0.28f;
		shape->orbitAxis = vec3(0.25f, 1.0f, 0.0f);
		shape->orbitRadius = 2.5f;
		shape->orbitSpeed = 0.55f;
		shape->orbitPhase = radians(120.0f * i);
		shape->spinAxis = vec3(1.0f, 0.7f, 0.3f);
		shape->spinSpeed = 1.1f;
		shape->bobHeight = 0.25f;
		shape->bobSpeed = 1.3f;
	};

	engine.Start();

	return 0;
};
