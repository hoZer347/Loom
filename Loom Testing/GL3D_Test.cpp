#include <GL/glew.h>
#include <GLFW/glfw3.h>

import Loom.TypedOGL;

void GL3D_Test()
{
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

	GLFWwindow* window = glfwCreateWindow(800, 600, "LearnOpenGL", NULL, NULL);
	if(window == NULL)
	{
		//std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		//return -1;
	}
	glfwMakeContextCurrent(window);

	glewInit();
	Loom::Rendering::SetClearColorValue(0.2f, 0.3f, 0.3f, 1.f);

	const char* vertexShaderSource = "#version 330 core\n"
		"layout (location = 0) in vec3 aPos;\n"
		"void main()\n"
		"{\n"
		"   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
		"}\0";

	const char* fragmentShaderSource = "#version 330 core\n"
		"out vec4 FragColor;\n"
		"void main()\n"
		"{\n"
		"FragColor = vec4(1.0f, 0.5f, 0.2f, 1.0f);\n"
		"}\0";

	auto vertexShader = Loom::Device::CreateAndCompileVertexShader(vertexShaderSource);
	auto fragmentShader = Loom::Device::CreateAndCompileFragmentShader(fragmentShaderSource);
	auto shaderProgram = Loom::Device::CreateShaderProgram(&vertexShader, nullptr, &fragmentShader);


	float vertices[] = {
		 0.5f,  0.5f, 0.0f,  // top right
		 0.5f, -0.5f, 0.0f,  // bottom right
		-0.5f, -0.5f, 0.0f,  // bottom left
		-0.5f,  0.5f, 0.0f   // top left 
	};

	Loom::Device::UniqueID<Loom::Device::ArrayBufferID> vertexBuffer = Loom::Device::CreateArrayBuffer(vertices, Loom::Device::BufferUsage::Static_Draw);

	auto VAO = Loom::Device::CreateVertexArrayObject(std::array{ Loom::Device::VertexAttribute
		{ 
			.index = 0,
			.size = 3,
			.type = Loom::Device::VertexAttributeType::Float,
			.normalized = false,
			.stride = sizeof(float) * 3,
			.offset = 0
		} });

	unsigned int indices[] = {  // note that we start from 0!
		0, 1, 3, };   // first triangle

	unsigned int indices2[] = {  // note that we start from 0!
		1, 2, 3, };   // first triangle

	auto EBO = Loom::Device::CreateElementArrayBuffer(indices, Loom::Device::BufferUsage::Static_Draw);
	auto EBO2 = Loom::Device::CreateElementArrayBuffer(indices2, Loom::Device::BufferUsage::Static_Draw);

	while(!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		Loom::Rendering::Clear(Loom::Rendering::ClearFlag::Color_Buffer);

		Loom::Rendering::BindShaderProgram(shaderProgram);
		Loom::Rendering::BindVertexArray(VAO);
		Loom::Rendering::BindBuffer(EBO.get());
		Loom::Rendering::DrawElements(Loom::Rendering::DrawMode::Triangles, 3, Loom::Rendering::ElementType::Unsigned_Int, 0);
		Loom::Rendering::BindBuffer(EBO2.get());
		Loom::Rendering::DrawElements(Loom::Rendering::DrawMode::Triangles, 3, Loom::Rendering::ElementType::Unsigned_Int, 0);
		Loom::Rendering::BindVertexArray({});
		glfwSwapBuffers(window);
	}

	glfwTerminate();
}

