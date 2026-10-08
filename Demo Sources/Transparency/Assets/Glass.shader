// Transparency demo shader: tinted glass, half see-through. Untick
// Transparency on a pane's Material and it is drawn in hierarchy order,
// writing depth, so it hides what is behind it rather than tinting it.

// ===SETTINGS===

shading = on
shadows = off
transparency = on

// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

void main()
{
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}

// ===FRAGMENT===

const float OPACITY = 0.45;

uniform vec3 u_color;

out vec4 FragColor;

void main()
{
	FragColor = vec4(u_color, OPACITY);
}
