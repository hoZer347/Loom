// Render target demo shader for the ground: the Material's colour, shaded,
// dimmed where the scouts have been but do not see now, and black where they
// have never been. u_visible and u_explored name the Visible and Explored
// GameObjects, whose RenderTargets the Scouts script draws into from above,
// laid over u_area: its least X and Z, then its greatest.

// ===SETTINGS===

shading = on
shadows = on

// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

out vec3 v_world;

void main()
{
	vec4 world = u_model * vec4(aPos, 1.0);

	v_world = world.xyz;
	gl_Position = u_viewProjection * world;
}

// ===FRAGMENT===

// How bright ground is that has been seen but is not seen now.
const float REMEMBERED = 0.3;

uniform sampler2D u_visible;
uniform sampler2D u_explored;
uniform vec4 u_area;
uniform vec3 u_color;

in vec3 v_world;

out vec4 FragColor;

void main()
{
	// Where this point is on the targets, as RenderTarget::Overhead lays them.
	vec2 texel = vec2(v_world.x - u_area.x, u_area.w - v_world.z) / (u_area.zw - u_area.xy);

	float visible = texture(u_visible, texel).r;
	float explored = texture(u_explored, texel).r;

	FragColor = vec4(u_color * mix(explored * REMEMBERED, 1.0, visible), 1.0);
}
