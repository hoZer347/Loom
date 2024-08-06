#include "State.h"

#include "Networking.h"

#include <boost/asio.hpp>

#include <iostream>

using boost::asio::ip::udp;


namespace Pong
{
	struct Input
	{
		short move_direction = 0;
	};

	static inline Input input{ };


	struct State : public Loom::Message
	{
		short score1 = 0, score2 = 0;
		vec3 player1_pos;
		vec3 player2_pos;
		vec3 puck_pos;
	};

	static inline State state{ };


	struct OtherPlayer : public Loom::Message
	{
		udp::endpoint endpoint;
	};

	void InterpretMessage(Loom::Message* message)
	{
		switch (message->message_type)
		{
		case PONG_UPDATE_STATE:
			state = *(Pong::State*)message;
			std::cout << "State Updated, Score1: " << state.score1 << std::endl;
			break;

		case PONG_GET_STATE:
			Loom::Send<PONG_UPDATE_STATE>(&state);
			break;

		case PONG_SEND_INPUT:
			input = *(Input*)message;
			break;
		};
	};

	void HandleFindOtherPlayer(Loom::Message* message)
	{
		OtherPlayer* other_player = (OtherPlayer*)message;
		std::cout << "Found other player at: " << other_player->endpoint << std::endl;
	};

	void Init()
	{
		Loom::SetHandle(PONG_UPDATE_STATE, InterpretMessage);
		Loom::SetHandle(PONG_GET_STATE, InterpretMessage);
		Loom::SetHandle(PONG_SEND_INPUT, InterpretMessage);

		boost::asio::io_service io_service{ };

		OtherPlayer* player0{ };
		OtherPlayer* player1{ };
		Loom::Request<PONG_FIND_OTHER_PLAYER>(player0, player1);
		
		udp::endpoint endpoint = player0->endpoint;

		udp::socket socket{ io_service, endpoint };
	};

	void GetState()
	{
		Loom::Send<PONG_GET_STATE>(&state);
	};

	void UpdateState()
	{
		Loom::Send<PONG_UPDATE_STATE>(&state);
	};
};
