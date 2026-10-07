// Shadows demo shader.
//
// Vertices arrive in the GameObject's space; u_model takes them to world space
// and the Camera's view-projection the rest of the way. Normals are not in the
// vertex format; the fragment stage takes one from the screen-space slope of
// the surface, which for flat faces is exact and always faces the viewer. The
// colour is the Material's.
//
// LoomLightSpace and LoomLight are the engine's: calling them is what has it
// put the light and shadow code in front of the stage when this compiles.

// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_viewProjection;
uniform mat4 u_model;

out vec3 v_world;
out vec4 v_lightSpace;

void main()
{
	vec4 world = u_model * vec4(aPos, 1.0);

	v_world = world.xyz;
	v_lightSpace = LoomLightSpace(world);
	gl_Position = u_viewProjection * world;
}

// ===FRAGMENT===

in vec3 v_world;
in vec4 v_lightSpace;

uniform vec3 u_color;

out vec4 FragColor;

void main()
{
	vec3 normal = normalize(cross(dFdx(v_world), dFdy(v_world)));

	FragColor = vec4(u_color * LoomLight(normal, v_lightSpace), 1.0);
}
