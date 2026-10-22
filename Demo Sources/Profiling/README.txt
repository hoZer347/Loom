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
camera, one row a frame, and draws a yellow tick along the surface normal at
every hit. After each full sweep it writes how long the rays took to the
Console. It is on thread 1, so its rays are cast on a thread of their own
beside the Crowd's updates on the main one.

In a Debug build the Stats panel times every stage of the frame on every
thread, each named after the function it times. Its timeline, graph and table
show where the frame goes: drawing the scene under Engine::onGui >
Editor::DrawGameView > Scene::Render > GameObject::Render, and the workers'
share of each. The physics starts under Scene::BeginPhysics, finds and solves
its contacts under Simulation::Resolve on the Physics thread while the GUI is
drawn, and moves the bodies under Simulation::EndAll once the GUI is done.
The Scanner's rays are cast under Scene::Update on Update 1.
