#pragma once

#include "Loom API.h"

#include <filesystem>


namespace Loom
{
	/**
	* Loom::ExecutablePath
	* - The running program's own file, empty in a browser, where there is none
	*/
	LOOM_API std::filesystem::path ExecutablePath();
};
