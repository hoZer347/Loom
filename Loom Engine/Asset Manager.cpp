import AssetManager;


namespace Loom
{
	void AssetManager::Start(const std::string& path)
	{
		file_path = path;
		isRunning = true;

		thread = std::thread(
			[&]()
			{
				while (isRunning)
				{

				};
			});
	};

	void AssetManager::Stop()
	{
		isRunning = false;
		if (thread.joinable())
			thread.join();
	};
};
