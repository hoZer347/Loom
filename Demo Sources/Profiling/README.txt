Profiling
=========

A Loom project heavy enough to time. Press Play and open the Stats panel.

    Scenes/Crowd.loomscene    fifteen hundred rigid bodies dropped into a pit
    Scripts/Crowd.hpp         drops boxes and balls with Rigidbodies
    Scripts/Scanner.hpp       casts a row of rays a frame through the camera
    Scripts/Shape.hpp         draws a cube, sphere or ring for its collider
    Assets/                   the shaders they draw with

On Play, Crowd drops fifteen hundred boxes and balls, each with a collider
and a Rigidbody, onto three rings with MeshColliders and into a walled pit,
where they pile up and fall asleep. Scanner sweeps 160 x 90 rays through the
camera, one row a frame.

In a Debug build the Stats panel times every stage of the frame on every
thread, each named after the function it times. Its timeline, graph and table
show where the frame goes: drawing the scene under Engine::onGui >
Editor::DrawGameView > Scene::Render > GameObject::Render, the physics under
Scene::Physics > Simulation::Step, and the workers' share of each.
