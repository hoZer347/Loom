// Compute demo shader: each particle Swarm.shader moved, as a square facing the
// camera, read straight from the buffer it wrote and coloured by its speed.

// ===SETTINGS===

shading = off
shadows = off

// ===VERTEX===

// Two to a particle: its position, then its velocity.
layout(std430) readonly buffer Particles { vec4 particles[]; };

uniform mat4 u_model;
uniform mat4 u_viewProjection;

out vec3 v_color;

const int CORNERS = 6;
const vec2 CORNER[CORNERS] = vec2[CORNERS](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
const float SIZE = 0.03;
const float FAST = 4.0;
const vec3 SLOW_COLOR = vec3(0.2, 0.4, 1.0);
const vec3 FAST_COLOR = vec3(1.0, 0.6, 0.2);

void main()
{
	int i = gl_VertexID / CORNERS * 2;
	vec3 velocity = particles[i + 1].xyz;
	vec4 clip = u_viewProjection * u_model * vec4(particles[i].xyz, 1.0);

	// How far across clip space a unit of view space spans, which a view
	// matrix's rows keep at the projection's own scale.
	vec2 scale = vec2(length(vec3(u_viewProjection[0][0], u_viewProjection[1][0], u_viewProjection[2][0])),
		length(vec3(u_viewProjection[0][1], u_viewProjection[1][1], u_viewProjection[2][1])));

	clip.xy += CORNER[gl_VertexID % CORNERS] * SIZE * scale;
	gl_Position = clip;
	v_color = mix(SLOW_COLOR, FAST_COLOR, clamp(length(velocity) / FAST, 0.0, 1.0));
}

// ===FRAGMENT===

in vec3 v_color;

out vec4 FragColor;

void main()
{
	FragColor = vec4(v_color, 1.0);
}
