Compute
=======

A Loom project showing a ComputeShader running on the GPU.

    Scenes/Main.loomscene          a plane under the camera
    Assets/Pattern.loomcompute     a reaction spreading over 256 by 256 cells
    Assets/Pattern.shader          what the plane draws with
    Assets/Fonts                   the explanation's font and its licence

The Pattern GameObject's ComputeShader runs Pattern.loomcompute eight steps
a frame, each cell reading its neighbours from the step before, and drops
more of the eater in somewhere new every 90 runs. The plane's shader reads
the cells through "uniform highp sampler2D Pattern; // compute", named for
the file, so nothing passes through the CPU. The Explanation panel says
what is happening, in the Game view.
