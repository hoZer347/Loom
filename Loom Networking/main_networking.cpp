import Networking;

using namespace Loom;

#include <thread>


int main()
{
	std::thread(OpenServerOnThisThread).detach();
	std::this_thread::sleep_for(std::chrono::seconds(1));
	std::thread(OpenClientOnThisThread).detach();

	while (true);
};
