#version 330 core

layout (location = 0) in vec3 aPos;

uniform mat4 mvp = mat4(1);

void main()
{
   gl_Position = vec4(aPos, 1.0) * mvp;
};
