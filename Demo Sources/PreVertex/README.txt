PreVertex
=========

A Loom project showing a PreVertex component moving the vertices of the
shaders it covers before their own vertex stage runs.

    Scenes/Main.loomscene        nine cubes on a finely divided lit floor
    Assets/Wave.loomprevertex    the code the Wave GameObject's PreVertex runs
    Assets/Lit.shader            what the floor draws with
    Assets/Pulse.shader          what the cubes draw with

Wave.loomprevertex lifts each vertex by a sine of its world X, on u_time,
so the floor and the cubes roll together. With Everything on it covers
every shader. Turned off, it covers only the shaders in its list, here
Lit.shader, so the floor keeps rolling and the cubes stand still.
