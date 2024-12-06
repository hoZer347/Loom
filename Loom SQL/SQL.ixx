#include <stdio.h>
#include "sqlite/sqlite3.h"

export module SQLite;

import <string>;

namespace Loom
{
	export struct SQLDB
	{
		// C++ Wrapper for SQLite3
		// Might be expanded to incorporate more functionality

		SQLDB(const std::string& file_name = "database.db");
		~SQLDB();

		void Request(const std::string& request);

	private:
		const std::string file_name;
		sqlite3* db = nullptr;
	};
};
