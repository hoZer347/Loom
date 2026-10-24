#ifdef __EMSCRIPTEN__

#include "WebCanvas.h"

#include "Input.h"

#include <GLFW/glfw3.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include "imgui_impl_glfw.h"


namespace Loom::WebCanvas
{
	const char* const SELECTOR = "#canvas";

	namespace
	{
		GLFWwindow* followed = nullptr;
		void (*onResized)(int, int) = nullptr;

		void Resized()
		{
			const glm::ivec2 size = Size();

			glfwSetWindowSize(followed, size.x, size.y);
			Input::OnResize(size.x, size.y);

			if (onResized)
				onResized(size.x, size.y);
		};
	};

	glm::ivec2 Size()
	{
		int width = 0;
		int height = 0;

		emscripten_get_canvas_element_size(SELECTOR, &width, &height);

		return glm::ivec2(width, height);
	};

	bool HasWebGPU()
	{
		return emscripten_run_script_int("!!navigator.gpu") != 0;
	};

	void FollowResizes(GLFWwindow* window, void (*resized)(int width, int height))
	{
		followed = window;
		onResized = resized;

		emscripten_set_resize_callback(
			EMSCRIPTEN_EVENT_TARGET_WINDOW,
			nullptr,
			EM_TRUE,
			[](int, const EmscriptenUiEvent*, void*) -> EM_BOOL
			{
				Resized();
				return EM_TRUE;
			});
	};

	void InstallImGui(GLFWwindow* window)
	{
		ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, SELECTOR);

		// That sizes the canvas to its box without a resize event, and the
		// page may have sent its own while WebGPU's adapter was awaited,
		// before anyone listened.
		Resized();

		emscripten_run_script(R"JS(
			(function(){
				var c = Module['canvas'];
				if (!c) return;
				c.tabIndex = 0;
				c.style.outline = 'none';
				c.style.touchAction = 'none';
				c.focus();
				c.addEventListener('click', () => c.focus());
				c.addEventListener('contextmenu', e => e.preventDefault());
				c.addEventListener('wheel', e => e.preventDefault(), { passive: false });
			})();
		)JS");
	};
};

#endif
