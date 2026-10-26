ShaderTime
==========

A Loom project showing the uniforms the engine gives every shader for time
and randomness.

    Scenes/Main.loomscene     nine alike cubes on a lit plane
    Assets/Pulse.shader       what the cubes draw with
    Assets/Lit.shader         what the plane draws with

Pulse.shader reads u_time, the seconds of game time since the start, and
u_seed, a number from 0 to 1 each GameObject keeps from its guid. The cubes
share one mesh and one colour, so they are drawn as instances of one, yet
each takes its own hue from its seed and pulses on the clock out of step
with the others. Pausing or slowing the game's clock stops or slows them.
