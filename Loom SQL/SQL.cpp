import SQLite;

#include <iostream>

#include "sqlite/sqlite3.h"


namespace Loom
{
	SQLDB::SQLDB(const std::string& file_name)
	{
		if (int rc = sqlite3_open(file_name.c_str(), &db))
		{
			std::cerr << "SQL: could not open " << file_name << " (" << rc << ')' << std::endl;
			return;
		};

		// Set the page size (must be set before setting max page count)
		const int page_size = 4096; // Page size in bytes
		const int max_page_count = (100 * 1024 * 1024) / page_size; // Limit to 100 MB

		// Set the page size using PRAGMA
		std::string page_size_query = "PRAGMA page_size = " + std::to_string(page_size) + ";";
		if (int rc = sqlite3_exec(db, page_size_query.c_str(), nullptr, nullptr, nullptr)) {
			std::cerr << "SQL: could not set the page size (" << rc << ')' << std::endl;
		}

		// Set the maximum page count to enforce the 100 MB limit
		std::string max_page_count_query = "PRAGMA max_page_count = " + std::to_string(max_page_count) + ";";
		if (int rc = sqlite3_exec(db, max_page_count_query.c_str(), nullptr, nullptr, nullptr))
		{
			std::cerr << "SQL: could not set the page count (" << rc << ')' << std::endl;
		};
	};

	SQLDB::~SQLDB()
	{
#if CLEAR_DB_ON_CLOSE
		sqlite3_db_config(db, SQLITE_DBCONFIG_RESET_DATABASE, 1, 0);
		sqlite3_exec(db, "VACUUM", 0, 0, 0);
		sqlite3_db_config(db, SQLITE_DBCONFIG_RESET_DATABASE, 0, 0);
#endif

		if (int rc = sqlite3_close(db))
		{
			std::cerr << "SQL: could not close the database (" << rc << ')' << std::endl;
			return;
		};
	};

	int callback(void*, int argc, char** argv, char** azColName)
	{
		for (int i = 0; i < argc; i++)
			printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
		
		return 0;
	};

	void SQLDB::Request(const std::string& request)
	{
		char* err;
		sqlite3_exec(
			db,
			request.c_str(),
			callback,
			0,
			&err);

		if (err)
		{
			std::cerr << "SQL: " << err << std::endl;
			sqlite3_free(err);
		};
	};
};
