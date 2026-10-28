Physics
=======

A Loom project showing rigidbodies. Press Play.

    Scenes/Main.loomscene     boxes and balls raining onto a ramp, a post, a pillar, a ring and an orb
    Scripts/Spawner.hpp       drops each with its collider and a Rigidbody, and lets the oldest go
    Scripts/Shape.hpp         draws a cube, sphere or ring for its collider
    Assets/                   the shader they draw with

Every body falls under gravity, bounces off the colliders it lands on and
settles into the pile. The ring's MeshCollider follows its own triangles, so
bodies fall through its hole. The scene runs on its own and reads no input.
