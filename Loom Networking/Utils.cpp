#include "Utils.hpp"

#include <iostream>


namespace Loom
{
	RAIIPersistentThreadedObject::RAIIPersistentThreadedObject() :
		thread(
			[&]()
			{
				while (isRunning)
					try
					{
						Update();
					}
					catch (const std::exception& ex)
					{
						std::cerr << ex.what() << std::endl;
					};
			})
	{ };

	RAIIPersistentThreadedObject::~RAIIPersistentThreadedObject()
	{
		isRunning = false;
		if (thread.joinable())
			thread.join();
	};

	RAIIPersistentNetworkObject::~RAIIPersistentNetworkObject()
	{
		io_context.stop();
	};

	void RAIIPersistentThreadedObject::WaitUntilDone()
	{
		if (thread.joinable())
			thread.join();
	};
};
