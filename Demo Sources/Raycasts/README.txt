Raycasts
========

A Loom project showing colliders and raycasts. Press Play.

    Scenes/Main.loomscene     a laser among boxes and spheres
    Scenes/Crowd.loomscene    fifteen hundred rigid bodies dropped into a pit
    Scripts/Crowd.hpp         drops boxes and balls with Rigidbodies
    Scripts/Scanner.hpp       casts a row of rays a frame through the camera
    Scripts/Laser.hpp         casts a ray every frame with Loom::Physics::Raycast
    Scripts/Shape.hpp         draws a cube, sphere or ring for its collider
    Scripts/Slider.hpp        keeps one block moving
    Assets/                   the shaders they draw with

Every shape carries a BoxCollider, a SphereCollider or, on the ring, a
MeshCollider that follows the ring's own triangles, so the beam goes through
its hole. The laser turns, stops its beam at the first collider in its way,
tints what it hit and draws the surface's normal from the hit point.

Select a shape in the Hierarchy to see its collider outlined in green in the
Scene view.

Crowd.loomscene is the stress test. On Play, Crowd drops fifteen hundred boxes
and balls, each with a collider and a Rigidbody, onto three rings with
MeshColliders and into a walled pit, where they pile up and fall asleep.
Scanner sweeps 160 x 90 rays through the camera, one row a frame, and draws a
yellow tick along the surface normal at every hit. After each full sweep it
writes how long the rays took to the Console.
