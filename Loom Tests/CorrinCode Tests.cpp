#include "doctest.h"

#include "CorrinCode.h"

#include <filesystem>
#include <fstream>


TEST_SUITE("CorrinCode")
{
	TEST_CASE("the override wins, then the install folder, else nothing")
	{
		using namespace Loom;

		const std::filesystem::path root = std::filesystem::temp_directory_path() / "Loom CorrinCode Tests";
		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root / "Corrin Code");

		const std::filesystem::path override_exe = root / "Other.exe";
		std::ofstream(override_exe).put('x');

		CHECK(CorrinCode::Find("", root).empty());

		std::ofstream(root / "Corrin Code" / "Corrin Code.exe").put('x');

		CHECK(CorrinCode::Find("", root) == root / "Corrin Code" / "Corrin Code.exe");
		CHECK(CorrinCode::Find(override_exe.string(), root) == override_exe);
		CHECK(CorrinCode::Find((root / "Missing.exe").string(), root) == root / "Corrin Code" / "Corrin Code.exe");
		CHECK(CorrinCode::Find("", "").empty());

		std::filesystem::remove_all(root);
	};
};
