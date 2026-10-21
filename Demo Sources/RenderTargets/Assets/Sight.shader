// Render target demo shader for what a scout sees: a disc, full in the
// middle and fading to nothing at its rim, which the Scouts script draws
// into the Visible and Explored targets. Its mesh is a square from -1 to 1
// across X and Z, scaled to the scout's sight.

// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

out vec2 v_across;

void main()
{
	v_across = aPos.xz;
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}

// ===FRAGMENT===

// How far out the rim starts to fade, as a share of the radius.
const float RIM = 0.6;

in vec2 v_across;

out vec4 FragColor;

void main()
{
	float seen = 1.0 - smoothstep(RIM, 1.0, length(v_across));

	FragColor = vec4(vec3(seen), 1.0);
}
