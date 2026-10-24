#ifdef __EMSCRIPTEN__

#include "WebCanvas.h"

#include "Engine.h"
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
		void (*onResized)(int, int) = nullptr;
	};

	glm::ivec2 Size()
	{
		int width = 0;
		int height = 0;

		emscripten_get_canvas_element_size(SELECTOR, &width, &height);

		return glm::ivec2(width, height);
	};

	void FollowResizes(void (*resized)(int width, int height))
	{
		onResized = resized;

		emscripten_set_resize_callback(
			EMSCRIPTEN_EVENT_TARGET_WINDOW,
			nullptr,
			EM_TRUE,
			[](int, const EmscriptenUiEvent*, void*) -> EM_BOOL
			{
				const glm::ivec2 size = Size();

				glfwSetWindowSize(Engine::window, size.x, size.y);
				Input::OnResize(size.x, size.y);

				if (onResized)
					onResized(size.x, size.y);

				return EM_TRUE;
			});
	};

	void InstallImGui(GLFWwindow* window)
	{
		ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, SELECTOR);

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
