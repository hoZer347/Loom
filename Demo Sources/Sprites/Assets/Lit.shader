// Ground shader for the Sprites demo, the same as the Shadows demo's.

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
