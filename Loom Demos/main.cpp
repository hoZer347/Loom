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

#include "OpenGL.h"
#include "Engine.h"
#include "Component.h"
#include "Demos.h"
#include "Scene.h"
#include "Shaders.h"
#include "Mesh.h"
#include "Material.h"
#include "DataPackage.h"
#include "UDPServer.h"
#include "TCPServer.h"
#include "TCPClient.h"

#include "glm/glm.hpp"
#include <filesystem>

using namespace Loom;
using namespace glm;


struct Buh : public DataPackage<Buh, 0>, public Component<Buh>
{
	void Handle() override
	{
		std::cout << c << std::endl;
	};
	
	void OnUpdate() override
	{
		if (glfwGetKey(Engine::window, GLFW_KEY_SPACE))
		{
			TCPClient::Send(this);
		};
	};

	char c[5] = "Test";
};

struct InitialConnection : public DataPackage<InitialConnection, 1>
{
	void Handle() override
	{
		std::cout << "Initial connection received!" << std::endl;
	};
};

int main()
{
	Engine engine;
	Scene scene{ "Server" };
	
	auto* server = scene.Attach<TCPServer>();
	auto* client = scene.Attach<TCPClient>();

	Buh* buh = scene.Attach<Buh>();
	buh->c[0] = 'A';
	buh->c[1] = 'A';
	buh->c[2] = 'A';
	buh->c[3] = 'A';
	buh->c[4] = 'A';

	TCPClient::Send(buh);

	engine.Start();
	return 0;
};
