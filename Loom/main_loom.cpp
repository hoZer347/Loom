import Component;
import Engine;
import Scene;
import Geometry;

#include <thread>

using namespace Loom;


struct Buh : public Component
{ };

int main()
{
	Engine engine	{ };

	std::thread t([&]() { engine.Start(); });

	Scene scene0	{ "Scene 0" };
	Scene scene1	{ "Scene 1" };
	Scene scene2	{ "Scene 2" };
	Scene scene3	{ "Scene 3" };

	//scene0.root->Attach<Rect>(100, 100, 100, 100);

	t.join();

	return 0;
};
