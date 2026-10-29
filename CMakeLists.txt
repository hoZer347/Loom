cmake_minimum_required(VERSION 3.28)
project(Loom LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# Beside the Windows builds, so an executable finds the engine three folders up as there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/x64/${CMAKE_BUILD_TYPE}")

# FieldNames reads members from the debug info, as Windows reads them from the PDB.
string(APPEND CMAKE_CXX_FLAGS_RELEASE " -g")

include(FetchContent)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
FetchContent_Declare(spirv_reflect GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git GIT_TAG vulkan-sdk-1.4.313.0 GIT_SHALLOW TRUE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)
FetchContent_Populate(spirv_reflect)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(Vulkan REQUIRED)
find_package(glslang REQUIRED)
find_package(assimp REQUIRED)
find_package(Threads REQUIRED)
find_package(X11 REQUIRED)

set(LIBS "${CMAKE_SOURCE_DIR}/External Libraries")

add_library(LoomImGui STATIC
	"Loom ImGui/imgui.cpp"
	"Loom ImGui/imgui_demo.cpp"
	"Loom ImGui/imgui_draw.cpp"
	"Loom ImGui/imgui_impl_glfw.cpp"
	"Loom ImGui/imgui_impl_opengl3.cpp"
	"Loom ImGui/imgui_impl_vulkan.cpp"
	"Loom ImGui/imgui_tables.cpp"
	"Loom ImGui/imgui_widgets.cpp"
	"Loom ImGui/TextEditor.cpp"
	"Loom ImGui/ImGuiItemHooks.cpp")
target_include_directories(LoomImGui PUBLIC "Loom ImGui" "${LIBS}/glm")
target_link_libraries(LoomImGui PUBLIC glfw GLEW::GLEW OpenGL::GL Vulkan::Vulkan)

file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS "Loom WASM/*.cpp" "Loom WASM/Utilities/*.cpp")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/_[^/]*\\.cpp$")
add_library(LoomEngine STATIC ${ENGINE_SOURCES} "${spirv_reflect_SOURCE_DIR}/spirv_reflect.c")
target_include_directories(LoomEngine PUBLIC
	"Loom WASM"
	"${LIBS}/stb"
	"${LIBS}/miniaudio"
	"${LIBS}/doctest"
	"${LIBS}/json/single_include"
	"${spirv_reflect_SOURCE_DIR}")
target_link_libraries(LoomEngine PUBLIC LoomImGui glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits Threads::Threads ${CMAKE_DL_LIBS})

function(vcxproj_sources project out)
	get_filename_component(dir "${project}" DIRECTORY)
	# So adding a file to the project reconfigures the build.
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${project}")
	file(STRINGS "${project}" lines REGEX "<ClCompile Include=\"[^\"*$]+\"")
	set(sources)
	foreach(line IN LISTS lines)
		string(REGEX REPLACE ".*<ClCompile Include=\"([^\"]+)\".*" "\\1" path "${line}")
		string(REPLACE "\\" "/" path "${path}")
		list(APPEND sources "${dir}/${path}")
	endforeach()
	set(${out} ${sources} PARENT_SCOPE)
endfunction()

vcxproj_sources("${CMAKE_SOURCE_DIR}/Loom Tests/Loom Tests.vcxproj" TEST_SOURCES)
add_executable(LoomTests ${TEST_SOURCES})
target_include_directories(LoomTests PRIVATE "Loom Tests" "Loom Editor")
target_compile_definitions(LoomTests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
target_link_libraries(LoomTests PRIVATE LoomEngine assimp::assimp dw X11::X11)
set_target_properties(LoomTests PROPERTIES OUTPUT_NAME "Loom Tests")

vcxproj_sources("${CMAKE_SOURCE_DIR}/Loom Editor/Loom Editor.vcxproj" EDITOR_SOURCES)
add_executable(LoomEditor ${EDITOR_SOURCES})
target_include_directories(LoomEditor PRIVATE "Loom Editor")
# Whole, so a script finds every engine function, not just the ones the editor calls.
target_link_libraries(LoomEditor PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,LoomEngine>" assimp::assimp dw X11::X11)
# Scripts load as shared libraries that call back into the engine linked here.
set_target_properties(LoomEditor PROPERTIES OUTPUT_NAME "Loom Editor" ENABLE_EXPORTS ON)

add_executable(LoomPlayer "Loom Player/Player.cpp")
target_link_libraries(LoomPlayer PRIVATE LoomEngine)
set_target_properties(LoomPlayer PROPERTIES OUTPUT_NAME "Loom Player")
