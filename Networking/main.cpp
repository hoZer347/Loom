#include <boost/asio.hpp>
#include <iostream>
#include <string>

#include "Networking.h"
#include "UI.h"


int main()
{
	std::thread t0([]() { Loom::Server server{ }; });
	std::thread t1([]() { Loom::Client client{ }; });
	std::thread t2(Loom::StartImguiDemo);

	t0.join();
	t1.join();
	t2.join();

	return 0;
};
