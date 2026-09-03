#include "EditorLog.h"

#include <iostream>


namespace Loom
{
	EditorLog& EditorLog::Get()
	{
		static EditorLog log;
		return log;
	};

	EditorLog::~EditorLog()
	{
		Uninstall();
	};

	void EditorLog::Install()
	{
		std::scoped_lock lock(mutex);

		if (installed)
			return;

		previous_out = std::cout.rdbuf();
		previous_err = std::cerr.rdbuf();

		out_sink.log = this;
		out_sink.passthrough = previous_out;
		out_sink.is_error = false;

		err_sink.log = this;
		err_sink.passthrough = previous_err;
		err_sink.is_error = true;

		std::cout.rdbuf(&out_sink);
		std::cerr.rdbuf(&err_sink);

		installed = true;
	};

	void EditorLog::Uninstall()
	{
		std::scoped_lock lock(mutex);

		if (!installed)
			return;

		std::cout.rdbuf(previous_out);
		std::cerr.rdbuf(previous_err);

		installed = false;
	};

	void EditorLog::Clear()
	{
		std::scoped_lock lock(mutex);

		lines.clear();
	};

	void EditorLog::ForEach(const std::function<void(const std::string&, bool)>& visit) const
	{
		std::scoped_lock lock(mutex);

		for (const Line& line : lines)
			visit(line.text, line.is_error);
	};

	void EditorLog::Sink::Put(char c)
	{
		if (c == '\r')
			return;

		if (c == '\n')
		{
			log->lines.push_back({ pending, is_error });

			if (log->lines.size() > max_lines)
				log->lines.erase(
					log->lines.begin(),
					log->lines.begin() + (log->lines.size() - max_lines));

			pending.clear();
		}
		else pending += c;
	};

	int EditorLog::Sink::overflow(int c)
	{
		if (c == EOF)
			return !EOF;

		// A build runs on a worker thread and writes to the same stream the
		// editor does, so the half-built line is held under the lock too.
		std::scoped_lock lock(log->mutex);

		Put((char)c);

		return passthrough
			? passthrough->sputc((char)c)
			: c;
	};

	int EditorLog::Sink::sync()
	{
		std::scoped_lock lock(log->mutex);

		// std::endl flushes the stream, and the stream is this sink; without
		// passing that on, output to a redirected stdout never reaches the file.
		return passthrough
			? passthrough->pubsync()
			: 0;
	};

	std::streamsize EditorLog::Sink::xsputn(const char* s, std::streamsize n)
	{
		std::scoped_lock lock(log->mutex);

		for (std::streamsize i = 0; i < n; i++)
			Put(s[i]);

		return passthrough
			? passthrough->sputn(s, n)
			: n;
	};
};
