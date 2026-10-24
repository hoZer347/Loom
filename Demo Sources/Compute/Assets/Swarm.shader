// Compute demo shader: moves each particle a step, pulled towards two points
// that circle the middle out of step with each other.

// ===COMPUTE===

layout(local_size_x = 64) in;

// Two to a particle: its position, then its velocity.
layout(std430) buffer Particles { vec4 particles[]; };

uniform float u_time;
uniform float u_deltaTime;

const float ORBIT = 3.0;
const float TILT = 0.7;
const float PULL = 6.0;
const float SOFTENING = 0.5;
const float DRAG = 0.1;

vec3 Pull(vec3 position, vec3 towards)
{
	vec3 to = towards - position;
	return to * PULL / (dot(to, to) + SOFTENING);
}

void main()
{
	uint i = gl_GlobalInvocationID.x * 2u;
	vec3 position = particles[i].xyz;
	vec3 velocity = particles[i + 1u].xyz;

	vec3 a = vec3(cos(u_time), sin(u_time * TILT), sin(u_time)) * ORBIT;

	velocity += (Pull(position, a) + Pull(position, -a)) * u_deltaTime;
	velocity -= velocity * DRAG * u_deltaTime;
	position += velocity * u_deltaTime;

	particles[i] = vec4(position, 1.0);
	particles[i + 1u] = vec4(velocity, 0.0);
}
