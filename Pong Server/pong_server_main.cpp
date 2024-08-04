#include "Networking.h"

#include <thread>


int main()
{
	std::thread(Loom::StartServer).detach();

	while (true);

	return 0;
};
