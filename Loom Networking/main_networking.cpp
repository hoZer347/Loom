import Networking;

using namespace Loom;

#include <thread>


int main()
{
	std::thread(StartServer).detach();
	std::thread(StartClient).detach();

	while (true);
};
