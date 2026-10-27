#pragma once

#include "Loom API.h"

#include "Asset.h"
#include "Component.h"

#include "glm/glm.hpp"

#include <cstdint>
#include <string>

struct ma_engine;
struct ma_sound;


namespace Loom
{
	// What Audio.cpp has miniaudio decode.
	using SoundAsset = Asset<".wav", ".mp3", ".flac">;

	/**
	* Loom::Audio
	* - Plays sound files (wav, mp3 or flac, by path relative to the project
	*   folder, as Sprite's textures are) through the default output device,
	*   opened the first time anything plays or an AudioSource loads
	* - Sounds are heard from the first enabled AudioListener among the open
	*   scenes, or from the world origin, facing -Z, while there is none
	* - The output pauses while Engine::updateScenes is off, which is how the
	*   editor's Pause holds every sound where it is
	* - A file PlayAt or Play is given is decoded the first time, and kept
	*   until Unload, so a sound played over and over is read once
	*/
	struct LOOM_API Audio final
	{
		#pragma region Shots

		// Plays path through once from position and lets it go. False when the
		// file will not load or there is no output, having said which the first
		// time.
		static bool PlayAt(const std::string& path, const glm::vec3& position, float volume = 1.0f);

		// The same, unpositioned: as loud wherever the listener is.
		static bool Play(const std::string& path, float volume = 1.0f);

		// Stops whatever PlayAt and Play started. An AudioSource keeps its own.
		static void StopAll();

		// Stops them, and forgets every file they decoded or failed to, so the
		// next play reads it again: for leaving a project, whose paths another
		// project's files would otherwise be mistaken for, or after a file has
		// changed.
		static void Unload();

		// How many of the sounds PlayAt and Play started are still going.
		static size_t PlayingCount();

		#pragma endregion

		#pragma region Output

		// Lets go of finished sounds, holds every AudioSource whose object was
		// not updated this frame, moves the listener to the AudioListener's
		// pose, and follows Engine::updateScenes. The engine calls it once a
		// frame, after updating the scenes.
		static void Tick();

		// With useDevice off, mixes frames as a device would have asked for
		// them, so sounds move on. Nothing moves while the output is paused.
		static void Mix(uint64_t frames);

		// Whether sounds go to a device. Set before the first sound: off still
		// loads and plays them, only nothing is heard, for a test or a tool that
		// has nobody to play to.
		static inline bool useDevice = true;

		#pragma endregion

	private:
		friend struct AudioSource;

		#pragma region Output

		// Opened the first time it is asked for. Null when it would not open.
		static ma_engine* Get();

		#pragma endregion
	};

	/**
	* Loom::AudioListener
	* - Where sounds are heard from: its GameObject's position, facing that
	*   object's -Z with its +Y up, the way a Camera looks
	* - Only the first enabled one among the open scenes listens
	*/
	struct LOOM_API AudioListener : Component<AudioListener, "Audio">
	{
		// The one that listens, or null.
		static AudioListener* Current();
	};

	/**
	* Loom::AudioSource
	* - A sound file played from its GameObject's position, which it follows
	*   as the object moves
	* - Its fields take effect on the next update. A new clipPath carries on
	*   playing if the old one was
	* - Held where it is while its GameObject is disabled, and carries on when
	*   it is enabled again
	*/
	struct LOOM_API AudioSource : Component<AudioSource, "Audio">
	{
		#pragma region Lifecycle

		AudioSource();
		~AudioSource();

		void OnUpdate() override;

		#pragma endregion

		#pragma region Playback

		// From the start, whether or not it was playing.
		void Play();
		void Stop();
		bool IsPlaying() const;

		#pragma endregion

		// Relative to the project folder, which is where the editor works from.
		Serial<SoundAsset> clipPath;
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
		friend struct Audio;

		#pragma region Sound

		// The sound for clipPath, loading it again whenever the path changes.
		// Null while it will not load.
		ma_sound* Load();

		// Hands the fields and the object's position to the sound.
		void Sync();

		void Release();

		// Holds the sound while the object was not updated this frame, and lets
		// it carry on once it is again.
		void Hold();

		#pragma endregion

		ma_sound* m_sound = nullptr;

		// What was last asked for, loaded or not, so a file that will not load
		// is reported once rather than every frame.
		std::string m_loaded_path;

		bool m_started = false;
		bool m_updated = false;
		bool m_held = false;
	};
};
