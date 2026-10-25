// Pixel pass demo shader: lit and shadowed like the Shadows demo, then, once
// the view is drawn, inked where the surface turns or steps away, and
// softened with distance.

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

// ===PIXEL PASS===

out vec4 FragColor;

PixelKernel9x9 around;
NormalKernel3x3 normals;
PositionKernel3x3 positions;

const vec3 INK = vec3(0.08, 0.06, 0.05);

// The cosine between neighbouring normals below which the surface has turned.
const float TURN = 0.8;

// How far a neighbour may sit from the camera beyond the centre, for each
// unit the centre is from it, before the surface has stepped away.
const float STEP = 0.05;

// From the camera, where softening starts and where it is whole.
const float SHARP = 8.0;
const float SOFT = 16.0;

void main()
{
	vec4 soft = vec4(0.0);

	for (int i = 0; i < around.count; i++)
		soft += around.pixels[i] * around.weights[i];

	float distance = length(positions.centre - u_cameraPosition);
	vec3 colour = mix(around.centre.rgb, soft.rgb, smoothstep(SHARP, SOFT, distance));

	float edge = 0.0;

	if (normals.centre != vec3(0.0))
		for (int i = 0; i < normals.count; i++)
		{
			bool turned = dot(normals.centre, normals.normals[i]) < TURN;
			bool stepped = length(positions.positions[i] - u_cameraPosition) - distance > STEP * distance;

			if (turned || stepped)
				edge = 1.0;
		}

	FragColor = vec4(mix(colour, INK, edge), 1.0);
}
