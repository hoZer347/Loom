#pragma once

#include "UndoHistory.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::AssetTrash
	* - What the Project panel deletes, held outside the project, where no
	*   scripts project compiles it, so Ctrl+Z can put it back and Ctrl+Y can
	*   take it again
	* - Whatever it still holds when emptied goes to the Recycle Bin, from
	*   where it was when that is free, so the bin puts it back there
	*/
	struct AssetTrash final
	{
		using Discard = std::function<void(const std::string& path)>;

		// The Recycle Bin, where the platform has one.
		static void Recycle(const std::string& path);

		explicit AssetTrash(Discard discard = Recycle) : m_discard(std::move(discard)) { };
		~AssetTrash() { Empty(); };

		AssetTrash(const AssetTrash&) = delete;
		AssetTrash& operator=(const AssetTrash&) = delete;

		// Takes the files and folders out as one step, all of them or none.
		bool Delete(const std::vector<std::string>& paths, std::string* error = nullptr);

		// The paths the next step moves, or none.
		std::vector<std::string> Next(bool undo) const;

		// Puts the last step back, or takes it again. Nothing is replaced: with
		// something at one of its paths, the step stays where it is.
		bool Step(bool undo, std::string* error = nullptr);

		bool CanUndo() const { return m_steps.CanUndo(); };
		bool CanRedo() const { return m_steps.CanRedo(); };

		// Discards all it holds and every step.
		void Empty();

	private:
		struct Entry
		{
			std::string path;
			std::string held;
		};

		// Moves each out to held or back to its path, or none of them.
		bool Move(const std::vector<Entry>& entries, bool out, std::string* error);

		Discard m_discard;
		UndoHistory<std::vector<Entry>> m_steps{ };

		// By where each is held, to where it came from.
		std::map<std::string, std::string> m_held{ };

		std::filesystem::path m_folder{ };
		size_t m_taken = 0;
	};
};
