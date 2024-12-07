#define _CRTDBG_MAP_ALLOC
#include <iostream>
#include <stdlib.h>
#include <crtdbg.h>

#include "imgui.h"
#include "Macro Helpers.h"

#include "../Loom Networking/Server.h"

import SQLite;
import <vector>;
import <thread>;

using namespace Loom;


int main()
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	std::string project_directory = "C:/Users/3hoze/Desktop/Loom Server Files";
	auto server = TCPServer(project_directory);
	auto db = SQLDB(project_directory + "/database.db");

	db.Request("CREATE TABLE IF NOT EXISTS test (id INTEGER PRIMARY KEY, name TEXT);");
	db.Request("INSERT INTO test (name) VALUES ('test');");
	db.Request("SELECT * FROM test;");

	std::string str;
	std::cout << "Enter q to quit" << std::endl;
	while (str != "q") std::cin >> str;

	return 0;
};
