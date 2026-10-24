# glslang, Tint and SPIRV-Reflect built with emscripten for the web engine,
# which compiles GLSL to SPIR-V with glslang and SPIR-V to WGSL with Tint. Configured and
# built by Build-WebShaderTools in WebBuild.ps1, with DAWN naming the Dawn
# checkout it fetched.
cmake_minimum_required(VERSION 3.22)
project(LoomWebShaderTools C CXX)

set(CMAKE_CXX_STANDARD 20)

set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_HEADERS_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)

add_subdirectory(${DAWN}/third_party/spirv-headers/src spirv-headers EXCLUDE_FROM_ALL)
add_subdirectory(${DAWN}/third_party/spirv-tools/src spirv-tools EXCLUDE_FROM_ALL)
add_subdirectory(${DAWN} dawn EXCLUDE_FROM_ALL)
add_subdirectory(${DAWN}/third_party/glslang/src glslang EXCLUDE_FROM_ALL)

# Where an installed glslang keeps them, beside the build_info.h its build
# writes, so the engine includes them as it does from the Vulkan SDK.
file(COPY ${DAWN}/third_party/glslang/src/SPIRV DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/include/glslang FILES_MATCHING PATTERN "*.h")

add_library(LoomWgsl STATIC Wgsl.cpp)
tint_core_compile_options(LoomWgsl)
target_link_libraries(LoomWgsl PRIVATE dawn_utils_config tint_lang_spirv_reader tint_lang_wgsl_writer)

# What GlslCompiler.cpp reflects SPIR-V with, from the Vulkan SDK as the
# desktop build has it.
add_library(SpirvReflect STATIC "${CMAKE_CURRENT_SOURCE_DIR}/../External Libraries/Vulkan/Source/SPIRV-Reflect/spirv_reflect.c")

# Linking an executable builds every library the engine links, which the
# build script then gathers from the build directory.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/Everything.cpp "int main() { return 0; }\n")
add_executable(Everything ${CMAKE_CURRENT_BINARY_DIR}/Everything.cpp)
target_link_libraries(Everything PRIVATE LoomWgsl SpirvReflect glslang glslang-default-resource-limits SPIRV)
