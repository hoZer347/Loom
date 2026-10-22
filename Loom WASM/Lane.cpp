#include "Lane.h"

#include "Profiler.h"


namespace Loom
{
	namespace
	{
		thread_local bool t_onLane = false;
	};

	Lane::Lane(const std::string& name, int order) :
		m_thread([this, name, order]() { Run(name, order); })
	{
	};

	Lane::~Lane()
	{
		Wait();

		m_stopping = true;
		m_generation++;
		m_generation.notify_one();

		m_thread.join();
	};

	void Lane::Start(const Work& work)
	{
		Wait();

		m_work = work;
		m_busy = true;
		m_generation++;
		m_generation.notify_one();
	};

	void Lane::Wait()
	{
		while (m_busy)
			m_busy.wait(true);
	};

	bool Lane::OnOne()
	{
		return t_onLane;
	};

	void Lane::Run(const std::string& name, int order)
	{
		t_onLane = true;

		Profiler::NameThread(name, order);

		for (unsigned seen = 0; ; seen++)
		{
			m_generation.wait(seen);

			if (m_stopping)
				return;

			m_work();

			m_busy = false;
			m_busy.notify_all();
		};
	};
};
