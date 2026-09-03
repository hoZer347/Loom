#pragma once

#include "Component.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <functional>
#include <iostream>
#include <string>
#include <queue>
#include <mutex>

#ifndef TCP_HOST_IP_PORT
#define TCP_HOST_IP_PORT 8000
#endif

#ifndef UDP_HOST_IP_PORT
#define UDP_HOST_IP_PORT 8000
#endif

namespace Loom
{
	struct Server
	{ };
};
