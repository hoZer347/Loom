#pragma once

#include <boost/system/error_code.hpp>
#include <boost/asio.hpp>
#include <thread>


namespace Loom
{
	// Maintains a looping update function that starts and ends with the lifetime of this object
	struct RAIIPersistentThreadedObject
	{
		virtual ~RAIIPersistentThreadedObject();

		void WaitUntilDone();

	protected:
		RAIIPersistentThreadedObject();
		virtual void Update() = 0;

	private:
		std::thread thread;
		bool isRunning = true;
	};

	// Maintains a looping update function that starts and ends with the lifetime of this object
	// Contains a io_context for network operations
	struct RAIIPersistentNetworkObject :
		public RAIIPersistentThreadedObject
	{
		virtual ~RAIIPersistentNetworkObject();	

		boost::asio::io_context io_context{ };
	};
};
