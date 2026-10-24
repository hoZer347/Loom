#include "GameBuilder.h"

#include <iostream>


namespace Loom
{
	bool GameBuilder::Launch(const std::string& arguments)
	{
		// Before the start, which stops the last run and its reader, so only
		// this one's reader writes it.
		m_status = Status::Building;

		const bool started = m_run.Start("build-game.ps1", arguments,
			[this](HiddenProcess hidden, const std::atomic<bool>& stopping)
			{
				hidden.ReadLines(
					[&](const std::string& line)
					{
						if (!stopping && !line.empty())
							std::cerr << "[build] " << line << std::endl;
					});

				const unsigned long code = hidden.Finish();

				if (!stopping)
					m_status = code == 0 ? Status::Built : Status::Failed;
			});

		if (!started)
			m_status = Status::Failed;

		return started;
	};
};
