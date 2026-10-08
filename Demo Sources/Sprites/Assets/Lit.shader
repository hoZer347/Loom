// Ground shader for the Sprites demo, the same as the Shadows demo's.
//
// Vertices arrive in the GameObject's space; u_model takes them to world space
// and the Camera's view-projection the rest of the way. Normals are not in the
// vertex format; the fragment stage takes one from the screen-space slope of
// the surface, which for flat faces is exact and always faces the viewer. The
// colour is the Material's.

// ===COMMON===

precision highp float;
precision highp sampler2DShadow;

// ===VERTEX===

layout(location = 0) in vec3 aPos;

uniform mat4 u_viewProjection;
uniform mat4 u_lightViewProjection;
uniform mat4 u_model;

out vec3 v_world;
out vec4 v_lightSpace;

void main()
{
	vec4 world = u_model * vec4(aPos, 1.0);

	v_world = world.xyz;
	v_lightSpace = u_lightViewProjection * world;
	gl_Position = u_viewProjection * world;
}

// ===FRAGMENT===

in vec3 v_world;
in vec4 v_lightSpace;

uniform vec3 u_color;

uniform vec3 u_lightDirection;
uniform vec3 u_lightColor;
uniform float u_ambient;
uniform sampler2DShadow u_shadowMap;

out vec4 FragColor;

// Taps either side of the centre texel, on top of the 2x2 the hardware
// comparison already filters.
const int PCF_RADIUS = 1;

// Clip space runs -1 to 1; the shadow map's texture and depth coordinates run
// 0 to 1.
const float CLIP_TO_TEXTURE = 0.5;

// How much of the light reaches this fragment, from 0 (shadowed) to 1.
float Visibility()
{
	vec3 coords = v_lightSpace.xyz / v_lightSpace.w * CLIP_TO_TEXTURE + CLIP_TO_TEXTURE;

	// Outside the shadow map is outside what the light can say anything about.
	if (any(lessThan(coords, vec3(0.0))) || any(greaterThan(coords, vec3(1.0))))
		return 1.0;

	vec2 texel = 1.0 / vec2(textureSize(u_shadowMap, 0));

	float lit = 0.0;
	float taps = 0.0;

	for (int x = -PCF_RADIUS; x <= PCF_RADIUS; x++)
		for (int y = -PCF_RADIUS; y <= PCF_RADIUS; y++)
		{
			lit += texture(u_shadowMap, vec3(coords.xy + vec2(x, y) * texel, coords.z));
			taps += 1.0;
		}

	return lit / taps;
}

void main()
{
	vec3 normal = normalize(cross(dFdx(v_world), dFdy(v_world)));
	float diffuse = max(dot(normal, -u_lightDirection), 0.0);

	vec3 color = u_color * (u_ambient + diffuse * Visibility() * u_lightColor);

	FragColor = vec4(color, 1.0);
}
