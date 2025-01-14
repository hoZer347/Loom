#version 330 core

in vec3 aPos;

uniform mat4 mvp;
uniform vec2 screen_dimensions;
uniform vec2 mouse_position;

void main()
{
	mat4 _mvp = mvp;

	_mvp[3][0] =
		mouse_position.x /
		screen_dimensions.x
		
		- 0.5f;

   gl_Position = vec4(aPos, 1.0) * _mvp;
};
