import Engine;
import Buffer;

#include "Buffer.cuh"
#include "imgui.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>


namespace Loom
{
	Buffer::Buffer(Engine* engine) :
		engine(engine)
	{ };

	Buffer::~Buffer()
	{ };

	void Buffer::OnRender()
	{ };

	void Buffer::OnGui()
	{ };
};
