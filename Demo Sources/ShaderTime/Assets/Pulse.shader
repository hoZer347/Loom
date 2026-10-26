// ShaderTime demo shader: each GameObject's own hue from u_seed, pulsing on
// u_time out of step with the rest, shaded and shadowed by the engine.

// ===SETTINGS===

shading = on
shadows = on

// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

void main()
{
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}

// ===FRAGMENT===

uniform vec3 u_color;
uniform float u_time;
uniform float u_seed;

out vec4 FragColor;

const float TAU = 6.28318531;

// Radians a second.
const float PULSE_SPEED = 3.0;

// How bright a cube is at the bottom of its pulse.
const float DIMMEST = 0.25;

// Where red, green and blue peak round the colour wheel.
const vec3 HUE_PHASES = vec3(0.0, 1.0 / 3.0, 2.0 / 3.0);

void main()
{
	vec3 hue = 0.5 + 0.5 * cos(TAU * (u_seed + HUE_PHASES));
	float pulse = 0.5 + 0.5 * sin(u_time * PULSE_SPEED + u_seed * TAU);

	FragColor = vec4(u_color * hue * mix(DIMMEST, 1.0, pulse), 1.0);
}
