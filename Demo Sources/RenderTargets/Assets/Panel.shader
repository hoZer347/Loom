// Render target demo shader for the panels behind the ground: the texture
// u_target names, as it is and unlit. Each panel's material names one of the
// Visible and Explored GameObjects, so it shows what the Scouts script has
// drawn into that one's RenderTarget.

// ===VERTEX===

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

out vec2 v_uv;

void main()
{
	v_uv = aUV;
	gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}

// ===FRAGMENT===

uniform sampler2D u_target;

in vec2 v_uv;

out vec4 FragColor;

void main()
{
	FragColor = vec4(texture(u_target, v_uv).rgb, 1.0);
}
