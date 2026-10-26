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
		#pragma region Installing

		static EditorLog& Get();

		void Install();
		void Uninstall();

		#pragma endregion

		#pragma region Lines

		// A file in the project that a line names, with where in it the line
		// points when it says.
		struct Link
		{
			// The bytes of the line's text it covers, the line and column with it.
			size_t start = 0;
			size_t length = 0;

			std::string path;

			// Counted from one, and zero when the line gives none.
			int line = 0;
			int column = 0;
		};

		struct Line
		{
			std::string text;
			bool is_error = false;
			std::vector<Link> links;
		};

		void Clear();

		// Lines that name a file inside this folder name it from the folder,
		// and link to it when it is there.
		void SetProject(const std::string& folder);

		// Walks the buffered lines under the lock, so the console can draw them
		// without copying the whole buffer every frame. A line keeps its id
		// while older lines are trimmed or cleared away.
		void ForEach(const std::function<void(size_t id, const Line&)>& visit) const;

		#pragma endregion

	private:
		#pragma region Installing

		EditorLog() = default;
		~EditorLog();

		#pragma endregion

		#pragma region Sink

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

		#pragma endregion

		static constexpr size_t max_lines = 2048;

		mutable std::recursive_mutex mutex;
		std::vector<Line> lines;
		size_t dropped = 0;
		std::string project;

		Sink out_sink, err_sink;
		std::streambuf* previous_out = nullptr;
		std::streambuf* previous_err = nullptr;
		bool installed = false;
	};
};
