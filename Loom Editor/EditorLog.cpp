#include "EditorLog.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <optional>
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
			const auto found = std::search(
				text.begin() + from,
				text.end(),
				folder.begin(),
				folder.end(),
				[](char a, char b) { return Folded(a) == Folded(b); });

			return (size_t)(found - text.begin());
		};

		// Where a file's name may end inside a run EndsPath has not cut short.
		// A name may hold any of these, so the longest that is a file wins.
		bool MayEndName(char c)
		{
			return std::string_view(" (),;'.").find(c) != std::string_view::npos;
		};

		// The longest name from start to the end of the text, or to where a
		// name may end before that, that is a file in the project.
		std::optional<EditorLog::Link> FileLink(const std::string& project, const std::string& text, size_t start)
		{
			std::error_code code;

			for (size_t end = text.size(); end > start; end--)
			{
				if (end < text.size() && !MayEndName(text[end]))
					continue;

				std::filesystem::path path = std::filesystem::path(project) / text.substr(start, end - start);

				if (std::filesystem::is_regular_file(path, code))
					return EditorLog::Link{ start, end - start, path.make_preferred().string() };
			};

			return std::nullopt;
		};

		// A whole number at, moved past. Zero when there is none.
		int Number(const std::string& text, size_t& at)
		{
			int value = 0;

			const auto [end, error] = std::from_chars(text.data() + at, text.data() + text.size(), value);

			if (error != std::errc{ } || value < 1)
				return 0;

			at = (size_t)(end - text.data());

			return value;
		};

		// MSVC's "(line)" or "(line,column)" straight after a link, taken into it.
		void ReadLocation(const std::string& text, EditorLog::Link& link)
		{
			size_t at = link.start + link.length;

			if (at >= text.size() || text[at] != '(')
				return;

			at++;

			const int line = Number(text, at);

			if (!line)
				return;

			int column = 0;

			if (at < text.size() && text[at] == ',')
			{
				at++;
				column = Number(text, at);

				if (!column)
					return;
			};

			if (at >= text.size() || text[at] != ')')
				return;

			link.line = line;
			link.column = column;
			link.length = at + 1 - link.start;
		};

		EditorLog::Line Local(const std::string& text, const std::string& project, bool is_error)
		{
			EditorLog::Line local{ "", is_error, { } };

			if (project.empty())
			{
				local.text = text;
				return local;
			};

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

				local.text.append(text, from, at - from);
				from = after + 1;
				at = Find(text, from, project);

				const size_t start = local.text.size();

				for (; from < at && !EndsPath(text[from]); from++)
					local.text += IsSeparator(text[from]) ? '/' : text[from];

				if (std::optional<EditorLog::Link> link = FileLink(project, local.text, start))
					local.links.push_back(std::move(*link));
			};

			local.text.append(text, from);

			for (EditorLog::Link& link : local.links)
				ReadLocation(local.text, link);

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

	void EditorLog::ForEach(const std::function<void(size_t, const Line&)>& visit) const
	{
		std::scoped_lock lock(mutex);

		for (size_t i = 0; i < lines.size(); i++)
			visit(dropped + i, lines[i]);
	};

	void EditorLog::Sink::Put(char c)
	{
		if (c == '\r')
			return;

		if (c == '\n')
		{
			log->lines.push_back(Local(pending, log->project, is_error));

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
