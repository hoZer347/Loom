
// ===VERTEX===

layout(location = 0) in vec3 aPos;

void main()
{
    gl_Position = vec4(aPos, 1.0);
}


// ===FRAGMENT===

out vec4 FragColor;

void main()
{
    FragColor = vec4(0.35, 0.8, 0.45, 1.0);
}
