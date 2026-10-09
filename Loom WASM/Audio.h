#pragma once

#include "Loom API.h"

#include "Component.h"

#include "glm/glm.hpp"

#include <string>

struct ma_engine;
struct ma_sound;


namespace Loom
{
	/**
	* Loom::Audio
	* - Plays sound files (wav, mp3 or flac, by path relative to the project
	*   folder, as Sprite's textures are) through the default output device,
	*   opened the first time anything plays
	* - Sounds are heard from the first enabled AudioListener among the open
	*   scenes, or from the world origin, facing -Z, while there is none
	* - The output pauses while Engine::updateScenes is off, which is how the
	*   editor's Pause holds every sound where it is
	* - A file is decoded once, the first time it is played, and shared by
	*   everything that plays it after
	*/
	struct LOOM_API Audio final
	{
		// Plays path through once from position and lets it go. False when the
		// file will not load or there is no output, having said which.
		static bool PlayAt(const std::string& path, const glm::vec3& position, float volume = 1.0f);

		// The same, unpositioned: as loud wherever the listener is.
		static bool Play(const std::string& path, float volume = 1.0f);

		// Stops whatever PlayAt and Play started. An AudioSource keeps its own.
		static void StopAll();

		// How many of the sounds PlayAt and Play started are still going.
		static size_t PlayingCount();

		// Lets go of finished sounds, and follows Engine::updateScenes. The
		// engine calls it once a frame.
		static void Tick();

		// Whether sounds go to a device. Set before the first sound: off still
		// loads and plays them, only nothing is heard, for a test or a tool that
		// has nobody to play to.
		static inline bool useDevice = true;

	private:
		friend struct AudioListener;
		friend struct AudioSource;

		// Opened the first time it is asked for. Null when it would not open.
		static ma_engine* Get();
	};

	/**
	* Loom::AudioListener
	* - Where sounds are heard from: its GameObject's position, facing that
	*   object's -Z with its +Y up, the way a Camera looks
	* - Only the first enabled one among the open scenes listens
	*/
	struct LOOM_API AudioListener : Component<AudioListener, "Audio">
	{
		void OnUpdate() override;

		// The one that listens, or null.
		static AudioListener* Current();
	};

	/**
	* Loom::AudioSource
	* - A sound file played from its GameObject's position, which it follows
	*   as the object moves
	* - Its fields take effect on the next update, a new clipPath included
	*/
	struct LOOM_API AudioSource : Component<AudioSource, "Audio">
	{
		~AudioSource();

		void OnUpdate() override;

		// From the start, whether or not it was playing.
		void Play();
		void Stop();
		bool IsPlaying() const;

		// Relative to the project folder, which is where the editor works from.
		Serial<std::string> clipPath;
		Serial<float> volume = 1.0f;

		// A playback speed, so 2 is an octave up and twice as fast.
		Serial<float> pitch = 1.0f;
		Serial<bool> loop = false;

		// Starts on its first update, which in the editor is the first frame of Play.
		Serial<bool> playOnStart = true;

		// Off plays it as loud wherever the listener is, for music.
		Serial<bool> spatial = true;

		// Full volume inside minDistance, falling off with distance beyond, and
		// no quieter past maxDistance.
		Serial<float> minDistance = 1.0f;
		Serial<float> maxDistance = 500.0f;

	private:
		// The sound for clipPath, loading it again whenever the path changes.
		// Null while it will not load.
		ma_sound* Load();

		// Hands the fields and the object's position to the sound.
		void Sync();

		void Release();

		ma_sound* m_sound = nullptr;

		// What was last asked for, loaded or not, so a file that will not load
		// is reported once rather than every frame.
		std::string m_loaded_path;

		bool m_started = false;
	};
};
