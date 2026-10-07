#pragma once

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#else
#define BOOST_ALL_NO_LIB
#define BOOST_DISABLE_ABI_HEADERS
#pragma comment(lib, "glfw3.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")

#include <GL/glew.h>
#endif

#include <GLFW/glfw3.h>
