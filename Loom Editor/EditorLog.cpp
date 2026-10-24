#include "EditorLog.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string_view>


namespace Loom
{
	namespace
	{
		bool IsSeparator(char c)
		{
			return c == '/' || c == '\\';
		};

		// Windows paths compare without case, and either separator.
		char Folded(char c)
		{
			return IsSeparator(c) ? '/' : (char)std::tolower((unsigned char)c);
		};

		// Where a path inside the project ends: at a character no file name
		// holds, so a name with spaces in it is kept whole.
		bool EndsPath(char c)
		{
			return std::string_view("<>:\"|?*").find(c) != std::string_view::npos;
		};

		// The size of the text when the folder is not in it.
		size_t Find(const std::string& text, size_t from, const std::string& folder)
		{
			return (size_t)(std::search(text.begin() + from, text.end(), folder.begin(), folder.end(),
				[](char a, char b) { return Folded(a) == Folded(b); }) - text.begin());
		};

		std::string Local(const std::string& text, const std::string& project)
		{
			if (project.empty())
				return text;

			std::string local;
			size_t from = 0;
			size_t at = Find(text, from, project);

			while (at < text.size())
			{
				const size_t after = at + project.size();

				// The folder itself, or another whose name starts with it.
				if (after >= text.size() || !IsSeparator(text[after]))
				{
					at = Find(text, after, project);
					continue;
				};

				local.append(text, from, at - from);
				from = after + 1;
				at = Find(text, from, project);

				for (; from < at && !EndsPath(text[from]); from++)
					local += IsSeparator(text[from]) ? '/' : text[from];
			};

			local.append(text, from);

			return local;
		};
	};

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

		dropped += lines.size();
		lines.clear();
	};

	void EditorLog::SetProject(const std::string& folder)
	{
		std::scoped_lock lock(mutex);

		project = std::filesystem::path(folder).lexically_normal().generic_string();

		while (!project.empty() && IsSeparator(project.back()))
			project.pop_back();
	};

	void EditorLog::ForEach(const std::function<void(size_t, const std::string&, bool)>& visit) const
	{
		std::scoped_lock lock(mutex);

		for (size_t i = 0; i < lines.size(); i++)
			visit(dropped + i, lines[i].text, lines[i].is_error);
	};

	void EditorLog::Sink::Put(char c)
	{
		if (c == '\r')
			return;

		if (c == '\n')
		{
			log->lines.push_back({ Local(pending, log->project), is_error });

			if (log->lines.size() > max_lines)
			{
				const size_t excess = log->lines.size() - max_lines;

				log->lines.erase(log->lines.begin(), log->lines.begin() + excess);
				log->dropped += excess;
			};

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

		// Whatever the passthrough says: a pipe whose reader went away would
		// otherwise put the stream in error, and the log would stop hearing it.
		if (passthrough)
			passthrough->sputc((char)c);

		return c;
	};

	int EditorLog::Sink::sync()
	{
		std::scoped_lock lock(log->mutex);

		// std::endl flushes the stream, and the stream is this sink; without
		// passing that on, output to a redirected stdout never reaches the file.
		if (passthrough)
			passthrough->pubsync();

		return 0;
	};

	std::streamsize EditorLog::Sink::xsputn(const char* s, std::streamsize n)
	{
		std::scoped_lock lock(log->mutex);

		for (std::streamsize i = 0; i < n; i++)
			Put(s[i]);

		if (passthrough)
			passthrough->sputn(s, n);

		return n;
	};
};
