// Transparency demo shader for what is not glass: the Material's colour,
// shaded and shadowed.

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

out vec4 FragColor;

void main()
{
	FragColor = vec4(u_color, 1.0);
}
