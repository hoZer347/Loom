#pragma once

#ifdef __EMSCRIPTEN__

#include "glm/glm.hpp"

struct GLFWwindow;


namespace Loom
{
	/**
	* Loom::WebCanvas
	* - The page's canvas, which both web renderers draw into and the page
	*   sizes to its element at the display's pixel ratio
	*/
	namespace WebCanvas
	{
		// The canvas's element, as a CSS selector.
		extern const char* const SELECTOR;

		glm::ivec2 Size();

		// Keeps the window and Input at the canvas's size as the page resizes,
		// then hands the size to resized.
		void FollowResizes(void (*resized)(int width, int height) = nullptr);

		// ImGui's input from the canvas, which takes the focus, and keeps the
		// browser from handling its right clicks and wheel itself.
		void InstallImGui(GLFWwindow* window);
	};
};

#endif
