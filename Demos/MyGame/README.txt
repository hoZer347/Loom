MyGame
======

A Loom project. The editor opens the folder; the scenes are in Scenes/ and the
scripts are an ordinary C++ project in Scripts/.

    Scenes/Main.loomscene     opened when the project loads
    Scripts/MyGameScripts.sln    open this in Visual Studio to write scripts
    Assets/Shader.shader         what the starter scene draws with
    Build/                       where the compiled script library lands

Scripts are Loom components, one header each: add a .hpp to Scripts/ with

    struct Name : Loom::Component<Name> { ... };

in it and it is compiled and offered under Add Component, with nothing to
register. With the engine's Loom Visual Studio extension installed,
right-click in Solution Explorer and pick Add > Loom Script... to write one there. Each header is compiled on its own and they may include one another,
so anything defined outside a struct has to be inline. Compile them from the
editor (the Compile button in the Project panel,
or just press Play) or build the solution in Visual Studio - they are the same
build. The library links the editor's import library, so there is one engine in
the process rather than one per module.

Start Debugging (F5) builds the scripts and then starts the editor on this
project with the scene running, so breakpoints in them are hit.

Members declared as Loom::Serial<type> name = default; appear in the inspector
under their own name (m_speed shows as "Speed") and are written into the scene
file, in the order they were declared - which is also how the file names them,
so inserting one in the middle shifts the values already saved.
SpinningTriangle is there as a worked example.
