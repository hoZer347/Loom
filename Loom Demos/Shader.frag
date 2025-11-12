#version 460 core

precision mediump float;
out vec4 color;

uniform vec2 mouse_position;      // Mouse position in pixels
uniform vec2 screen_dimensions;  // Screen dimensions in pixels

vec2 complexSquare(vec2 z) {
    // Compute the square of a complex number z = x + yi
    return vec2(
        z.x * z.x - z.y * z.y,  // Real part: x^2 - y^2
        2.0 * z.x * z.y         // Imaginary part: 2xy
    );
}

float mandelbrot(vec2 c, vec2 dynamicC, int maxIterations) {
    vec2 z = vec2(0.0, 0.0); // Start with Z0 = 0
    int iteration = 0;

    for (int i = 0; i < maxIterations; i++) {
        z = complexSquare(z) + c + dynamicC; // Z(n+1) = Z(n)^2 + C + dynamicC
        
        if (dot(z, z) > 4.0) { // Escape condition: |z|^2 > 4
            break;
        }
        
        iteration++;
    }

    return float(iteration) / float(maxIterations); // Normalize the iteration count
}

void main() {
    // Map pixel coordinates to normalized device coordinates
    vec2 uv = gl_FragCoord.xy / screen_dimensions;

    // Map UV to complex plane
    vec2 center = vec2(-0.5, 0.0); // Default Mandelbrot center
    float zoom = 2.0; // Base zoom level
    vec2 c = center + (uv * 2.0 - 1.0) / zoom; // Map UV to complex plane

    // Dynamically modify the Mandelbrot equation using mouse position
    vec2 dynamicC = vec2(
        (mouse_position.x / screen_dimensions.x - 0.5) * 2.0,  // Map mouse X to [-1, 1]
        (mouse_position.y / screen_dimensions.y - 0.5) * 2.0   // Map mouse Y to [-1, 1]
    );

    // Perform Mandelbrot calculation
    float v = mandelbrot(c, dynamicC, 100); // Max iterations = 100

    // Generate dynamic color based on mouse position and iteration count
    vec3 gradientColor = vec3(
        0.5 + 0.5 * cos(6.2831 * v + dynamicC.x), // Red channel modulated by dynamicC.x
        0.5 + 0.5 * cos(6.2831 * v + dynamicC.y), // Green channel modulated by dynamicC.y
        0.5 + 0.5 * sin(6.2831 * v)              // Blue channel modulated by iteration
    );

    color = vec4(gradientColor, 1.0);
}
