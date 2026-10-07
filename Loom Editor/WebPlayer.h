#pragma once

#include <string>
#include <thread>


namespace Loom
{
	/**
	* Loom::WebPlayer
	* - Plays a scene in the browser: runs play-web.ps1 from the engine folder,
	*   which builds the project with emscripten and serves it, and opens the
	*   page in the default browser once the server is listening
	* - The script and everything it starts share a job object, so launching
	*   again or closing the editor takes the server down with it
	* - Everything the build says goes to the console, from a reader thread
	*/
	struct WebPlayer final
	{
		WebPlayer() = default;
		~WebPlayer();

		WebPlayer(const WebPlayer&) = delete;
		WebPlayer& operator=(const WebPlayer&) = delete;

		// Both absolute, and the scene under the project folder, which is what
		// the page is served from. Stops whatever an earlier launch left running.
		bool Launch(const std::string& project, const std::string& scene);

		void Stop();

		// Where the page is, given a line of the script's output: empty unless
		// it is the one emrun prints once it is listening,
		// "Now listening at http://<host>:<port>/". The host is the loopback
		// address it was asked for, and the page is a path from the server root.
		static std::string PageUrl(const std::string& line, const std::string& page)
		{
			const std::string listening = "Now listening at http://";

			const size_t at = line.find(listening);

			if (at == std::string::npos)
				return "";

			const std::string address = line.substr(at + listening.size());
			const size_t colon = address.rfind(':');

			if (colon == std::string::npos)
				return "";

			const size_t slash = address.find('/', colon);
			const std::string port = address.substr(colon + 1, slash == std::string::npos ? std::string::npos : slash - colon - 1);

			if (port.empty() || port.find_first_not_of("0123456789") != std::string::npos)
				return "";

			return "http://localhost:" + port + page;
		};

	private:
		void* m_job = nullptr;
		std::thread m_reader{ };
	};
};
