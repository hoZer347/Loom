// Draws Pattern.loomcompute's data across the plane, the eater bright on
// the dark of the chemical it eats.

// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

// The plane's half width, so its corners land on the data's.
const float HALF_WIDTH = 5.0;

out vec2 v_uv;

void main()
{
	v_uv = aPos.xz / (2.0 * HALF_WIDTH) + 0.5;
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}

// ===FRAGMENT===

uniform highp sampler2D Pattern; // compute

in vec2 v_uv;

out vec4 FragColor;

const vec3 BACKGROUND = vec3(0.04, 0.05, 0.12);
const vec3 EATER = vec3(0.95, 0.75, 0.3);

// How much of the eater shows fully bright.
const float BRIGHTEST = 0.35;

void main()
{
	float eater = texture(Pattern, v_uv).y;

	FragColor = vec4(mix(BACKGROUND, EATER, clamp(eater / BRIGHTEST, 0.0, 1.0)), 1.0);
}
