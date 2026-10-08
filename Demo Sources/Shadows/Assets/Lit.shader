// Shadows demo shader: the Material's colour, which the engine shades and
// shadows because the settings say so.

// ===SETTINGS===

shading = on
shadows = on

// ===FRAGMENT===

uniform vec3 u_color;

out vec4 FragColor;

void main()
{
	FragColor = vec4(u_color, 1.0);
}
