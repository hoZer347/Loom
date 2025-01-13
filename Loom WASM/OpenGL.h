#pragma once

#ifdef __EMSCRIPTEN__
#include <GLES2/gl2.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#else
#include <GL/glew.h>
#endif

#include <GLFW/glfw3.h>
