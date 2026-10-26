UVLayers
========

A Loom project showing a mesh's raw UV layers. Press Play.

    Scenes/Main.loomscene     hills on a grid
    Scripts/Terrain.hpp       builds the hills and packs three layers with Loom::Mesh::SetUV
    Assets/Terrain.shader     reads them back as UV_1, UV_2 and UV_3

Each layer is any list of types, one value a vertex, packed into raw words on
the CPU and read back by name in the vertex stage:

    C++                                                 shader
    SetUV<glm::vec3, float>(1, tuples)                  UV_1<vec3 tint, float glow>;  UV_1.tint
    SetUV<int>(2, bands)                                UV_2<int band>;               UV_2.band
    SetUV<bool>(3, lines)                               UV_3<bool>;                   UV_3

The tint follows the height, odd height bands are darker and every eighth row
and column is lighter. While playing, the script sets UV_1 again every frame,
so a ring of glow spreads out from the middle and starts again.
