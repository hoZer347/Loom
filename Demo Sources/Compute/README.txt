Compute
=======

A Loom project showing a compute shader. Press Play, then Play Web: the
swarm moves the same in the editor and in the browser.

    Scenes/Main.loomscene     a swarm of 16384 particles
    Scripts/Swarm.hpp         dispatches Swarm.shader each frame and draws the result
    Assets/Swarm.shader       a COMPUTE section that moves each particle a step
    Assets/Particle.shader    draws each particle from the buffer Swarm.shader writes

The particles live in one storage buffer. Swarm.shader writes it and
Particle.shader's vertex stage reads it as a readonly block, so nothing comes
back to the CPU. The same GLSL runs on desktop OpenGL and Vulkan and, through
WebGPU, in the browser. The web build's WebGL fallback has no compute and
draws nothing.
