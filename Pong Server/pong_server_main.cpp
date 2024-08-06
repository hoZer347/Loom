#define LOOM_SERVER

#include "Networking.h"
#include "State.h"

#include <thread>


int main()
{
	int* i = new int(5);

	std::thread(Loom::StartServer).detach();

	Pong::Init();

	while (true);

	return 0;
};
