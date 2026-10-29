#include "doctest.h"

// The editor runs on Windows and Linux, but the web test build compiles this too.
#ifndef __EMSCRIPTEN__

#include "FolderWatch.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>


namespace
{
	// Long enough for a notification the system queues to have arrived.
	constexpr std::chrono::milliseconds SETTLE{ 200 };

	constexpr std::chrono::seconds LIMIT{ 5 };

	struct Folder
	{
		std::filesystem::path path;

		explicit Folder(const std::string& name)
			: path(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		};

		~Folder()
		{
			std::error_code code;
			std::filesystem::remove_all(path, code);
		};
	};

	void Write(const std::filesystem::path& file, const std::string& text)
	{
		std::ofstream(file) << text;
	};

	// Polled as the editor polls it, once a frame, until it notices or gives up.
	bool NoticesWithin(Loom::FolderWatch& watch, std::chrono::milliseconds limit)
	{
		constexpr std::chrono::milliseconds FRAME{ 16 };

		const auto deadline = std::chrono::steady_clock::now() + limit;

		do
		{
			if (watch.Changed())
				return true;

			std::this_thread::sleep_for(FRAME);
		} while (std::chrono::steady_clock::now() < deadline);

		return false;
	};
};


TEST_SUITE("FolderWatch")
{
	TEST_CASE("a folder nothing touches has not changed")
	{
		const Folder folder("loom-folderwatch-quiet");
		Loom::FolderWatch watch;
		watch.Watch(folder.path.string(), true);

		CHECK_FALSE(NoticesWithin(watch, SETTLE));
	};

	TEST_CASE("a file added is noticed once, and a file saved only when writes are watched")
	{
		const Folder folder("loom-folderwatch-names");
		Write(folder.path / "kept.txt", "before");

		Loom::FolderWatch names;
		names.Watch(folder.path.string(), false);

		Write(folder.path / "added.txt", "");
		CHECK(NoticesWithin(names, LIMIT));
		CHECK_FALSE(NoticesWithin(names, SETTLE));

		Write(folder.path / "kept.txt", "after");
		CHECK_FALSE(NoticesWithin(names, SETTLE));

		Loom::FolderWatch writes;
		writes.Watch(folder.path.string(), true);

		Write(folder.path / "kept.txt", "again");
		CHECK(NoticesWithin(writes, LIMIT));
	};

	TEST_CASE("a file in a folder made after the watch began is noticed")
	{
		const Folder folder("loom-folderwatch-nested");
		Loom::FolderWatch watch;
		watch.Watch(folder.path.string(), false);

		std::filesystem::create_directories(folder.path / "Scripts");
		CHECK(NoticesWithin(watch, LIMIT));

		Write(folder.path / "Scripts" / "Spin.hpp", "");
		CHECK(NoticesWithin(watch, LIMIT));
	};

	TEST_CASE("an empty folder watches nothing")
	{
		Loom::FolderWatch watch;
		watch.Watch("", true);

		CHECK(watch.GetFolder().empty());
		CHECK_FALSE(watch.Changed());
	};
};

#endif
