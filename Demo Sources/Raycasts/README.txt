Raycasts
========

A Loom project showing colliders and raycasts. Press Play.

    Scenes/Main.loomscene     a laser among boxes and spheres
    Scripts/Laser.hpp         casts a ray every frame with Loom::Physics::Raycast
    Scripts/Shape.hpp         draws a cube, sphere or ring for its collider
    Scripts/Slider.hpp        keeps one block moving
    Assets/                   the shaders they draw with

Every shape carries a BoxCollider, a SphereCollider or, on the ring, a
MeshCollider that follows the ring's own triangles, so the beam goes through
its hole. The laser turns, stops its beam at the first collider in its way,
tints what it hit and draws the surface's normal from the hit point.

Select a box or sphere in the Hierarchy to see its collider outlined in green
in the Debug view.
