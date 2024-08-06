#pragma once

#include "glm/glm.hpp"
using namespace glm;

#define PONG_UPDATE_STATE 22
#define PONG_GET_STATE 23
#define PONG_SEND_INPUT 24
#define PONG_FIND_OTHER_PLAYER 25


namespace Pong
{
	void Init();
	void GetState();
	void UpdateState();
};
