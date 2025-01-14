## Welcome To the Loom Codebase!

- This is where I store any C++ projects I'm working on

- You'll find the most interesting code in Loom Engine, Networking, and WASM

### Included Libraries:

*External Libraries:*
- Having the libraries come with the codebase makes it more convenient than having a package manager

*Loom Demos:*
- Where I plan to put tests and demos for my projects

*Loom WASM*
- Project for compiling and running WASM programs in the browser
- compile.py: automatically compiles WASM projects based on a "Source Directories.txt", or its containing folder

*Loom Engine:*
- Skeleton for a Modern C++ Game Engine, built with ImGui and OpenGL as a backend
- Features thread safe memory management, and an Entity-Component system
- Uses modern C++ features such as modules and std::barrier
- NOTE: Currently postponed due to modules not being supported with WASM, see Loom WASM for a more developed version. I'm keeping it to show off my modern C++ skills

*Loom ImGui:*
- Imported ImGui implementation using an OpenGL backend

*Loom Math*
- glm substitute for WASM projects

*Loom Networking:*
- Backend server code using boost::asio sockets

*Loom SQL:*
- Basic SQL Server set up

*Property Files:*
- Various .prop files for easily importing libraries / projects into each other


### Bugs To Fix:

~~- Take glViewport out of the renderLoop (right now it's there so the window takes up the whole browser page on load)~~


### Planned Features:

#### Shader Improvements
- Automatic shader compilation
- Shader conversion (GLSL core 330 and es 300)
- Shader file combination (Unifying vertex / fragment etc, into a single file)
- Shader file template generation / automatic uniform updating (Generating a template shader file based on the uniforms connected to the shader)

#### Networking
- Implement SSL into Server struct
- Implement UDPServer
- Implement multi-threading on the browser

#### Miscellaneous
- Implement SQL
- ImGui implementation for debugging
- Add a CUDA Library

### What I'm working on right now:
- Adding a Fractal Background to loomhoZer.ca
- Adding shader features mentioned above