cmake_minimum_required(VERSION 3.28)
project(LoomScripts LANGUAGES CXX)

# A project's scripts as its vcxproj builds them on Windows: each header on its own, the engine's header precompiled and forced in.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
string(APPEND CMAKE_CXX_FLAGS_RELEASE " -g")

file(GLOB_RECURSE SCRIPTS CONFIGURE_DEPENDS "${LOOM_SCRIPTS}/*.hpp")

# A source that includes each header, since GCC compiles a header as a precompiled one.
set(SOURCES)
foreach(script IN LISTS SCRIPTS)
	cmake_path(RELATIVE_PATH script BASE_DIRECTORY "${LOOM_SCRIPTS}" OUTPUT_VARIABLE relative)
	set(source "${CMAKE_BINARY_DIR}/Sources/${relative}.cpp")
	file(CONFIGURE OUTPUT "${source}" CONTENT "#include \"${script}\"\n")
	list(APPEND SOURCES "${source}")
endforeach()

cmake_path(GET LOOM_LIBRARY PARENT_PATH LIBRARY_FOLDER)
cmake_path(GET LOOM_LIBRARY STEM LAST_ONLY LIBRARY_NAME)
set(LIBS "${LOOM_ROOT}/External Libraries")

add_library(Scripts SHARED ${SOURCES})
target_include_directories(Scripts PRIVATE
	"${LOOM_ROOT}/Loom WASM"
	"${LOOM_ROOT}/Loom ImGui"
	"${LIBS}/glm"
	"${LIBS}/glew/include"
	"${LIBS}/glfw/include"
	"${LIBS}/stb"
	"${LIBS}/miniaudio"
	"${LIBS}/doctest"
	"${LIBS}/json/single_include")
target_compile_definitions(Scripts PRIVATE LOOM_SCRIPT_MODULE)
# Unique symbols would pin the library in memory, so a rebuilt one could never replace it.
target_compile_options(Scripts PRIVATE -fno-gnu-unique)
target_precompile_headers(Scripts PRIVATE "${LOOM_ROOT}/Loom WASM/Loom.h")
set_target_properties(Scripts PROPERTIES PREFIX "" OUTPUT_NAME "${LIBRARY_NAME}" LIBRARY_OUTPUT_DIRECTORY "${LIBRARY_FOLDER}")
