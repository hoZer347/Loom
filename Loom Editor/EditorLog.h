#pragma once

#include <functional>
#include <iosfwd>
#include <mutex>
#include <streambuf>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::EditorLog
	* - The backing store for the editor's console
	* - While installed it mirrors everything written to std::cout / std::cerr,
	*   so existing engine logging shows up in the editor without any changes
	*/
	struct EditorLog final
	{
		static EditorLog& Get();

		void Install();
		void Uninstall();

		void Clear();

		// Walks the buffered lines under the lock, so the console can draw them
		// without copying the whole buffer every frame. A line keeps its id
		// while older lines are trimmed or cleared away.
		void ForEach(const std::function<void(size_t id, const std::string&, bool is_error)>& visit) const;

	private:
		EditorLog() = default;
		~EditorLog();

		// Splits whatever is written to a stream into lines and appends them,
		// passing the characters along to the stream's original buffer so the
		// console does not swallow output.
		struct Sink final :
			public std::streambuf
		{
			EditorLog* log = nullptr;
			std::streambuf* passthrough = nullptr;
			bool is_error = false;

		protected:
			int overflow(int c) override;
			std::streamsize xsputn(const char* s, std::streamsize n) override;
			int sync() override;

		private:
			void Put(char c);

			std::string pending;
		};

		struct Line
		{
			std::string text;
			bool is_error;
		};

		static constexpr size_t max_lines = 2048;

		mutable std::recursive_mutex mutex;
		std::vector<Line> lines;
		size_t dropped = 0;

		Sink out_sink, err_sink;
		std::streambuf* previous_out = nullptr;
		std::streambuf* previous_err = nullptr;
		bool installed = false;
	};
};
