// UV layers demo shader: the colour the Terrain script packs into its UV
// layers, darker on odd height bands, lighter on the grid lines, glowing
// where the ring passes, shaded and shadowed.

// ===SETTINGS===

shading = on
shadows = on

// ===VERTEX===

layout(location = 0) in vec3 aPos;

UV_1<vec3 tint, float glow>;
UV_2<int band>;
UV_3<bool>;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

out vec3 v_tint;
out float v_glow;
flat out int v_band;
flat out int v_line;

void main()
{
	v_tint = UV_1.tint;
	v_glow = UV_1.glow;
	v_band = UV_2.band;
	v_line = UV_3 ? 1 : 0;
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}

// ===FRAGMENT===

const vec3 GLOW = vec3(1.0, 0.55, 0.15);
const float ODD_BAND = 0.85;
const float LINE = 0.35;

in vec3 v_tint;
in float v_glow;
flat in int v_band;
flat in int v_line;

out vec4 FragColor;

void main()
{
	vec3 colour = v_tint * (v_band % 2 == 0 ? 1.0 : ODD_BAND);
	colour = mix(colour, vec3(1.0), float(v_line) * LINE);
	FragColor = vec4(colour + GLOW * v_glow, 1.0);
}
