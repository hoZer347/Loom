#include "doctest.h"

#include "Test Support.h"

#include "Audio.h"
#include "Engine.h"
#include "GameObject.h"
#include "Scene.h"

#include "glm/glm.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

using LoomTests::Pump;


#pragma region Helpers

namespace
{
	constexpr uint32_t SAMPLE_RATE = 48000;

	// Mixes without a device, so the suite plays nothing out loud and runs
	// where there is no output at all.
	struct AudioFixture : LoomTests::EngineFixture
	{
		AudioFixture()
		{
			Loom::Audio::useDevice = false;
		};

		// Pumped first so Tick does not look for a listener in the case's scenes.
		~AudioFixture()
		{
			Pump();

			Loom::Engine::updateScenes = true;
			Loom::Audio::Tick();
			Loom::Audio::Unload();
		};
	};

	// A tenth of a second of 16 bit mono silence, as a wav in the temp folder.
	std::string WriteWav(const std::string& name)
	{
		constexpr uint32_t FRAMES = SAMPLE_RATE / 10;
		constexpr uint16_t CHANNELS = 1;
		constexpr uint16_t BITS = 16;
		constexpr uint16_t PCM = 1;
		constexpr uint32_t FORMAT_SIZE = 16;
		constexpr uint32_t BYTES_PER_FRAME = CHANNELS * BITS / 8;
		constexpr uint32_t DATA_SIZE = FRAMES * BYTES_PER_FRAME;

		// Everything in the RIFF chunk after its own size: "WAVE", then the
		// format and data chunks with their eight byte headers.
		constexpr uint32_t CHUNK_HEADER = 8;
		constexpr uint32_t WAVE_ID = 4;
		constexpr uint32_t RIFF_SIZE = WAVE_ID + CHUNK_HEADER + FORMAT_SIZE + CHUNK_HEADER + DATA_SIZE;

		const std::string path = (std::filesystem::temp_directory_path() / name).string();
		std::ofstream file(path, std::ios::binary);

		const auto write = [&file](const auto& value) { file.write((const char*)&value, sizeof(value)); };

		file.write("RIFF", WAVE_ID);
		write(RIFF_SIZE);
		file.write("WAVEfmt ", WAVE_ID + WAVE_ID);
		write(FORMAT_SIZE);
		write(PCM);
		write(CHANNELS);
		write(SAMPLE_RATE);
		write(SAMPLE_RATE * BYTES_PER_FRAME);
		write((uint16_t)BYTES_PER_FRAME);
		write(BITS);
		file.write("data", WAVE_ID);
		write(DATA_SIZE);

		for (uint32_t frame = 0; frame < FRAMES; frame++)
			write((int16_t)0);

		return path;
	};
};

#pragma endregion


TEST_SUITE("Audio")
{
	#pragma region Playing

	TEST_CASE_FIXTURE(AudioFixture, "PlayAt and Play start a sound each, and StopAll stops them")
	{
		const std::string clip = WriteWav("loom-audio-shot.wav");
		const size_t before = Loom::Audio::PlayingCount();

		CHECK(Loom::Audio::PlayAt(clip, glm::vec3(1.0f, 2.0f, 3.0f)));
		CHECK(Loom::Audio::Play(clip, 0.5f));
		CHECK(Loom::Audio::PlayingCount() == before + 2);

		Loom::Audio::StopAll();
		CHECK(Loom::Audio::PlayingCount() == 0);
	};

	TEST_CASE_FIXTURE(AudioFixture, "a one-shot is let go once it has played through")
	{
		CHECK(Loom::Audio::Play(WriteWav("loom-audio-through.wav")));
		CHECK(Loom::Audio::PlayingCount() == 1);

		Loom::Audio::Mix(SAMPLE_RATE);
		CHECK(Loom::Audio::PlayingCount() == 0);
	};

	TEST_CASE_FIXTURE(AudioFixture, "sounds hold where they are while the scenes are paused")
	{
		const std::string clip = WriteWav("loom-audio-paused.wav");
		CHECK(Loom::Audio::Play(clip));

		Loom::Engine::updateScenes = false;
		Loom::Audio::Tick();
		Loom::Audio::Mix(SAMPLE_RATE);
		CHECK(Loom::Audio::PlayingCount() == 1);

		Loom::Engine::updateScenes = true;
		Loom::Audio::Tick();
		Loom::Audio::Mix(SAMPLE_RATE);
		CHECK(Loom::Audio::PlayingCount() == 0);
	};

	TEST_CASE_FIXTURE(AudioFixture, "a file is read once until Unload")
	{
		const std::string clip = WriteWav("loom-audio-unload.wav");
		CHECK(Loom::Audio::Play(clip));

		std::filesystem::remove(clip);
		CHECK(Loom::Audio::Play(clip));

		Loom::Audio::Unload();
		CHECK(Loom::Audio::PlayingCount() == 0);

		const LoomTests::Captured captured(std::cerr);

		CHECK_FALSE(Loom::Audio::Play(clip));
		CHECK(captured.Says("could not load " + clip));
	};

	TEST_CASE_FIXTURE(AudioFixture, "a file that will not load plays nothing")
	{
		const LoomTests::Captured captured(std::cerr);

		CHECK_FALSE(Loom::Audio::PlayAt("no such sound.wav", glm::vec3(0.0f)));
		CHECK(Loom::Audio::PlayingCount() == 0);
		CHECK(captured.Says("could not load no such sound.wav"));
	};

	#pragma endregion

	#pragma region Sources and Listeners

	TEST_CASE_FIXTURE(AudioFixture, "an AudioSource plays on its first update, and only then")
	{
		Loom::Scene scene("audio");
		Pump();

		Loom::AudioSource* source = scene.Attach<Loom::AudioSource>();
		source->clipPath = WriteWav("loom-audio-source.wav");
		Pump();

		CHECK_FALSE(source->IsPlaying());

		scene.Update();
		CHECK(source->IsPlaying());

		source->Stop();
		scene.Update();
		CHECK_FALSE(source->IsPlaying());

		source->Play();
		CHECK(source->IsPlaying());
	};

	TEST_CASE_FIXTURE(AudioFixture, "an AudioSource with playOnStart off waits for Play")
	{
		Loom::Scene scene("audio");
		Pump();

		Loom::AudioSource* source = scene.Attach<Loom::AudioSource>();
		source->clipPath = WriteWav("loom-audio-waiting.wav");
		source->playOnStart = false;
		Pump();

		scene.Update();
		CHECK_FALSE(source->IsPlaying());

		source->Play();
		CHECK(source->IsPlaying());
	};

	TEST_CASE_FIXTURE(AudioFixture, "an AudioSource is held while its object is disabled, and carries on when enabled")
	{
		Loom::Scene scene("audio");
		Loom::GameObject* holder = scene.AddChild("Speaker");
		Pump();

		Loom::AudioSource* source = holder->Attach<Loom::AudioSource>();
		source->clipPath = WriteWav("loom-audio-held.wav");
		Pump();

		scene.Update();
		Loom::Audio::Tick();
		CHECK(source->IsPlaying());

		holder->SetDisabled(true);
		scene.Update();
		Loom::Audio::Tick();
		CHECK_FALSE(source->IsPlaying());

		holder->SetDisabled(false);
		scene.Update();
		CHECK(source->IsPlaying());
	};

	TEST_CASE_FIXTURE(AudioFixture, "a new clipPath replaces the sound on the next update, playing if the old one was")
	{
		Loom::Scene scene("audio");
		Pump();

		Loom::AudioSource* source = scene.Attach<Loom::AudioSource>();
		source->clipPath = WriteWav("loom-audio-first.wav");
		Pump();

		scene.Update();
		CHECK(source->IsPlaying());

		source->clipPath = WriteWav("loom-audio-second.wav");
		scene.Update();
		CHECK(source->IsPlaying());

		const LoomTests::Captured captured(std::cerr);

		source->clipPath = "no such sound.wav";
		scene.Update();
		CHECK_FALSE(source->IsPlaying());
		CHECK(captured.Says("could not load no such sound.wav"));

		source->Play();
		CHECK_FALSE(source->IsPlaying());
	};

	TEST_CASE_FIXTURE(AudioFixture, "the first enabled AudioListener among the open scenes listens")
	{
		Loom::Scene first("first");
		Loom::Scene second("second");
		Pump();

		CHECK(Loom::AudioListener::Current() == nullptr);

		Loom::GameObject* holder = first.AddChild("Ears");
		Pump();

		Loom::AudioListener* near = holder->Attach<Loom::AudioListener>();
		Loom::AudioListener* far = second.Attach<Loom::AudioListener>();
		Pump();

		CHECK(Loom::AudioListener::Current() == near);

		holder->SetDisabled(true);
		CHECK(Loom::AudioListener::Current() == far);
	};

	#pragma endregion
};
