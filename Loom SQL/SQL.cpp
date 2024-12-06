import SQLite;

#include <iostream>

#include "sqlite/sqlite3.h"


namespace Loom
{
	SQLDB::SQLDB(const std::string& file_name)
	{
		if (int rc = sqlite3_open(file_name.c_str(), &db))
		{
			std::cerr << "Failed to open database, error code: " << rc << std::endl;
			return;
		};

		// Set the page size (must be set before setting max page count)
		const int page_size = 4096; // Page size in bytes
		const int max_page_count = (100 * 1024 * 1024) / page_size; // Limit to 100 MB

		// Set the page size using PRAGMA
		std::string page_size_query = "PRAGMA page_size = " + std::to_string(page_size) + ";";
		if (int rc = sqlite3_exec(db, page_size_query.c_str(), nullptr, nullptr, nullptr)) {
			std::cerr
				<< "Failed to set page size, error code: "
				<< rc
				<< std::endl;
		}

		// Set the maximum page count to enforce the 100 MB limit
		std::string max_page_count_query = "PRAGMA max_page_count = " + std::to_string(max_page_count) + ";";
		if (int rc = sqlite3_exec(db, max_page_count_query.c_str(), nullptr, nullptr, nullptr))
		{
			std::cerr
				<< "Failed to set max page count. Error code: "
				<< rc
				<< std::endl;
		};

		// Optional: Retrieve and print the applied limits
		sqlite3_stmt* stmt;
		if (sqlite3_prepare_v2(db, "PRAGMA max_page_count;", -1, &stmt, nullptr) == SQLITE_OK)
		{
			if (sqlite3_step(stmt) == SQLITE_ROW)
			{
				int applied_max_page_count = sqlite3_column_int(stmt, 0);
				std::cout
					<< "Database max size set to approximately "
					<< (applied_max_page_count * page_size) / (1024 * 1024)
					<< " MB."
					<< std::endl;
			};
			sqlite3_finalize(stmt);
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
			std::cerr << "Failed to close database, error code: " << rc << std::endl;
			return;
		};
	};

	int callback(void* data, int argc, char** argv, char** azColName)
	{
		printf("%s\n", (const char*)data);
		
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
			std::cerr << "Failed to execute request: " << err << std::endl;
			sqlite3_free(err);
		};
	};
};
