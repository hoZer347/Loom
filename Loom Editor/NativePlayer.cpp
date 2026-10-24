#include "NativePlayer.h"

#include <iostream>


namespace Loom
{
	bool NativePlayer::Launch(const std::string& project, const std::string& scene, std::string_view configuration)
	{
		return m_run.Start("play-native.ps1", Arguments(project, scene, configuration),
			[](HiddenProcess hidden, const std::atomic<bool>& stopping)
			{
				hidden.ReadLines(
					[&](const std::string& line)
					{
						if (!stopping && !line.empty())
							std::cerr << "[player] " << line << std::endl;
					});
			});
	};
};
