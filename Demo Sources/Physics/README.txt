Physics
=======

A Loom project showing rigidbodies. Press Play.

    Scenes/Main.loomscene     boxes and balls raining onto a ramp, a post, a pillar, a ring and an orb
    Scripts/Spawner.hpp       drops each with its collider and a Rigidbody, and lets the oldest go
    Scripts/Shape.hpp         draws a cube, sphere or ring for its collider
    Scripts/SpawnerTests.hpp  cases the Tests panel runs
    Assets/                   the shader they draw with

Every body falls under gravity, rolls off the ramp onto the orb and settles
into the pile around the ring, whose MeshCollider follows its own triangles.
The scene runs on its own and reads no input.
