using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography.X509Certificates;


string directory = "C:\\Users\\3hoze\\Desktop\\Loom Server Files";
string pfxFilePath = "C:/Users/3hoze/Desktop/Win-ACME/Certs/loomhozer.ca.pfx";
HashSet<string> uniqueIPs = new();

string pfxPassword = "";
int port = 443;

// Load the certificate
X509Certificate2 certificate = new(pfxFilePath, pfxPassword);
//X509Certificate2 certificate = X509CertificateLoader.LoadCertificate(File.ReadAllBytes(pfxFilePath));
// Start the server
TcpListener listener = new TcpListener(IPAddress.Any, 443); // Port 443 for HTTPS
listener.Start();
Console.WriteLine("SSL server is running and listening for connections...");

while (true)
{
	try
	{
		listener.Start();
		Console.WriteLine($"HTTPS server running on https://localhost:{port}");

		while (true)
		{
			// Accept incoming client connections
			TcpClient client = listener.AcceptTcpClient();

			Console.WriteLine($"Connection from: {client.Client.RemoteEndPoint}");

			// Handle the client in a new task
			_ = HandleClientAsync(client, certificate);
			//HandleClient(client, certificate);
		};
	}
	catch (Exception ex)
	{
		Console.WriteLine($"SSL error: {ex.Message}");
	};
};

async Task HandleClientAsync(TcpClient client, X509Certificate2 certificate)
{
	try
	{
		using (SslStream sslStream = new SslStream(client.GetStream(), false))
		{
			// Authenticate as server using TLS 1.2
			await sslStream.AuthenticateAsServerAsync(certificate, false, SslProtocols.Tls12, false);

			// Read the client's request
			using (StreamReader reader = new StreamReader(sslStream))
			using (StreamWriter writer = new StreamWriter(sslStream))
			{
				//string line;
				//while (!string.IsNullOrEmpty(line = reader.ReadLine()))
				//	Console.WriteLine(line);

				//writer.WriteLine("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 21\r\n\r\n<h1>Under Maintenance, connect back soon!</h1>");
				//writer.Flush();

				Handle(reader, writer);
			};
		};
	}
	catch (Exception ex)
	{
		Console.WriteLine($"Client error: {ex.Message}");
	}
	finally
	{
		client.Close();
	};
}

void HandleClient(TcpClient client, X509Certificate2 certificate)
{
	try
	{
		using (SslStream sslStream = new SslStream(client.GetStream(), false))
		{
			// Authenticate as server using TLS 1.2
			sslStream.AuthenticateAsServer(certificate, false, SslProtocols.Tls12, false);

			// Read the client's request
			using (StreamReader reader = new StreamReader(sslStream))
			using (StreamWriter writer = new StreamWriter(sslStream))
			{
				Handle(reader, writer);
			};
		};
	}
	catch (Exception ex)
	{
		Console.WriteLine($"Client error: {ex.Message}");
	}
	finally
	{
		client.Close();
	};
}

void Handle(
	StreamReader reader,
	StreamWriter writer)
{
	string firstLine = reader.ReadLine();
	if (firstLine == null)
	{
		writer.Flush();
		return;
	};

	var split = firstLine.Split(" ");

	string method = split[0];
	string path = split[1];
	string version = split[2];

	if (method.StartsWith("GET"))
		HandleGet(reader, writer, path);

	else if (method.StartsWith("POST"))
		HandlePost(reader, writer, path);
}

void HandleGet(
	StreamReader reader,
	StreamWriter writer,
	string path)
{
	path = path.Replace("%20", " ");

	Console.WriteLine($"Requested:       {path}");

	// Shortcut to index.html
	if (path == "/")
	{
		SendIndex(writer);
		return;
	};

	if (path.Contains("..") ||
		path.Contains("?") ||
		path.Contains("=") ||
		path.Contains("%"))

		throw new Exception("Dangerous path in request detected, exiting");

	if (!File.Exists(directory + path))
		throw new Exception("File not found");

	SendGet(reader, writer, path);
}

void SendIndex(StreamWriter writer)
{
	string content = File.ReadAllText(directory + "/index.html");

	string response = "";
	response += "HTTP/1.1 200 OK\r\n";
	response += "Content-Type: text/html\r\n";
	response += "Content-Length: " + content.Length + "\r\n";
	response += "\r\n";
	response += content + "\r\n";

	writer.Write(response);
	writer.Flush();

	Console.WriteLine("Sent:            index.html");
}

void SendGet(
	StreamReader reader,
	StreamWriter writer,
	string path)
{
	string response = "";
	string content_type = "";
	string content_encoding = "";
	bool isBinary = false;

	try
	{
		if (path.EndsWith(".html"))
			content_type = "text/html";
		else if (path.EndsWith(".css"))
			content_type = "text/css";
		else if (path.EndsWith(".json"))
			content_type = "text/json";
		else if (path.EndsWith(".js"))
			content_type = "application/javascript";
		else if (path.EndsWith(".txt") || path.EndsWith(".frag") || path.EndsWith(".vert"))
			content_type = "text/plain";
		else if (path.EndsWith(".wasm"))
		{
			content_type = "application/wasm";
			isBinary = true;
		}
		else if (path.EndsWith(".wasm.map"))
		{
			content_type = "application/wasm";
			isBinary = true;
		}
		else if (path.EndsWith(".png"))
		{
			content_type = "image/png";
			isBinary = true;
		}
		else if (path.EndsWith(".jpg") || path.EndsWith(".jpeg"))
		{
			content_type = "image/jpeg";
			isBinary = true;
		}
		else if (path.EndsWith(".png"))
		{
			content_type = "image/png";
			isBinary = true;
		}
		else if (path.EndsWith(".ico"))
		{
			content_type = "image/x-icon";
			isBinary = true;
		}
		else if (path.EndsWith(".data"))
		{
			content_type = "application/octet-stream";
			isBinary = true;
		}
		else if (path.EndsWith(".wasm.gz"))
		{
			content_type = "application/wasm";
			isBinary = true;
		}
		else if (path.EndsWith(".gz") || path.EndsWith(".data"))
		{
			content_type = "application/octet-stream";
			content_encoding = "gzip";
			isBinary = true;
		}
		else if (path.EndsWith(".br"))
		{
			content_type = "application/octet-stream";
			content_encoding = "br";
			isBinary = true;
		}
		else throw new Exception("File type not supported: " + directory + path);

		response = "HTTP/1.1 200 OK\r\n";
		response += "Content-Type: " + content_type + "\r\n";

		if (isBinary)
		{
			// Handle binary files
			byte[] content = File.ReadAllBytes(directory + path);
			response += "Content-Length: " + content.Length.ToString() + "\r\n";
			if (content_encoding != "") response += content_encoding + "\r\n";
			response += "\r\n";

			// Write headers and binary content
			writer.BaseStream.Write(System.Text.Encoding.ASCII.GetBytes(response));
			writer.BaseStream.Write(content);
			writer.BaseStream.Flush();
		}
		else
		{
			string content = File.ReadAllText(directory + path);
			response += "Content-Length: " + content.Length.ToString() + "\r\n";
			response += "\r\n";
			response += content;

			writer.Write(response);
			writer.Flush();
		};

		Console.WriteLine($"Sent:            {path}");
	}
	catch (Exception ex)
	{
		Console.WriteLine($"Error handling GET request for {path}: {ex.Message}");
		writer.WriteLine("HTTP/1.1 500 Internal Server Error\r\n\r\n");
		writer.Flush();
	}
}

void HandlePost(
	StreamReader reader,
	StreamWriter writer,
	string path)
{
	string line;
	while (!string.IsNullOrWhiteSpace(line = reader.ReadLine()))
		Console.WriteLine(line);

	writer.Flush();
}
