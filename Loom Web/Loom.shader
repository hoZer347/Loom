// Loom Web scene shader.
//
// WireShape does the model/view/projection on the CPU and hands us normalized
// device coordinates, so the vertex stage is a pass-through and the only thing
// left to shade on is depth. Near edges burn sky blue and far ones fall away to
// slate, which matches the portfolio the canvas sits behind.

// ===COMMON===

precision mediump float;

// ===VERTEX===

layout(location = 0) in vec3 aPos;

out float v_ndc_depth;

void main()
{
	gl_Position = vec4(aPos, 1.0);

	v_ndc_depth = aPos.z;
}

// ===FRAGMENT===

in float v_ndc_depth;

out vec4 FragColor;

uniform float u_time;
uniform float u_near;
uniform float u_far;

// Undo the perspective divide's effect on depth. NDC z is bunched hard against
// the far plane (at a 0.1 near plane, everything past a couple of units reads as
// ~1.0), so shading on it directly leaves the whole scene one flat colour. This
// recovers the actual view-space distance, which is spread evenly.
float LinearDepth(float ndc_z)
{
	return (2.0 * u_near * u_far) /
		(u_far + u_near - ndc_z * (u_far - u_near));
}

void main()
{
	const vec3 near_color = vec3(0.486, 0.827, 0.988); // sky-300
	const vec3 far_color  = vec3(0.192, 0.306, 0.451); // dimmed sky, still legible

	// Everything in the scene sits between about five and thirteen units in
	// front of the camera, so ramp across that rather than across the whole
	// near-to-far range, where it would all land on one value.
	float depth = LinearDepth(clamp(v_ndc_depth, -1.0, 1.0));
	float nearness = clamp((13.0 - depth) / 7.5, 0.0, 1.0);

	// Bias toward the bright end so only genuinely distant edges wash out.
	float fade = pow(nearness, 1.6);

	// A slow breath over the whole scene, so a still frame still reads as live.
	float pulse = 0.9 + 0.1 * sin(u_time * 0.8);

	vec3 color = mix(far_color, near_color, fade) * pulse;

	FragColor = vec4(color, 0.55 + 0.45 * fade);
}
