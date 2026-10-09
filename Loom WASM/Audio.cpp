#include "Audio.h"

#include "Engine.h"
#include "GameObject.h"
#include "Scene.h"

#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <algorithm>
#include <iostream>
#include <list>
#include <unordered_set>
#include <vector>


namespace Loom
{
	namespace
	{
		// The pitch the sound's clock stops at, and below.
		constexpr float MIN_PITCH = 0.01f;

		// What the mixer runs at with no device to ask.
		constexpr ma_uint32 SILENT_CHANNELS = 2;
		constexpr ma_uint32 SILENT_SAMPLE_RATE = 48000;

		// Decoded whole on the thread that loads them, so the resource manager
		// needs no threads of its own.
		constexpr ma_uint32 LOAD_FLAGS = MA_SOUND_FLAG_DECODE;

		constexpr ma_uint32 LISTENER = 0;

		// Where sounds are heard from with no AudioListener.
		constexpr glm::vec3 FORWARD(0.0f, 0.0f, -1.0f);
		constexpr glm::vec3 UP(0.0f, 1.0f, 0.0f);

		struct Output final
		{
			ma_resource_manager resources;
			ma_engine engine;

			// The sounds PlayAt and Play started. A list, so each stays where the
			// mixer is reading it while others come and go.
			std::list<ma_sound> shots;

			// The files PlayAt and Play have registered, which keeps them decoded
			// between one shot and the next.
			std::unordered_set<std::string> registered;

			// The files that would not load, so a sound played over and over is
			// reported once rather than every time.
			std::unordered_set<std::string> failed;

			bool running = true;
		};

		// Opened by Audio::Get and never closed: the mixer thread is reading it
		// until the process ends, and a scene destroyed by a static destructor
		// still lets go of its sources against it.
		Output* output = nullptr;
		bool opening_failed = false;

		// Every AudioSource there is. Never destroyed, for the same reason.
		std::vector<AudioSource*>& Sources()
		{
			static std::vector<AudioSource*>* const sources = new std::vector<AudioSource*>();
			return *sources;
		};

		// Removes every sound that has finished, or every sound when all is set.
		void Reap(bool all)
		{
			std::list<ma_sound>& shots = output->shots;

			for (auto shot = shots.begin(); shot != shots.end();)
				if (all || ma_sound_at_end(&*shot))
				{
					ma_sound_uninit(&*shot);
					shot = shots.erase(shot);
				}
				else ++shot;
		};

		bool Shoot(ma_engine* engine, const std::string& path, float volume, const glm::vec3* position)
		{
			if (!engine)
				return false;

			Reap(false);

			if (!output->registered.contains(path))
			{
				const ma_result result = ma_resource_manager_register_file(&output->resources, path.c_str(), MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);

				if (result != MA_SUCCESS)
				{
					if (output->failed.insert(path).second)
						std::cerr << "Audio: could not load " << path << ": " << ma_result_description(result) << std::endl;

					return false;
				};

				output->registered.insert(path);
			};

			ma_sound& shot = output->shots.emplace_back();
			const ma_result result = ma_sound_init_from_file(engine, path.c_str(), LOAD_FLAGS, nullptr, nullptr, &shot);

			if (result != MA_SUCCESS)
			{
				output->shots.pop_back();
				std::cerr << "Audio: could not play " << path << ": " << ma_result_description(result) << std::endl;

				return false;
			};

			ma_sound_set_volume(&shot, volume);

			if (position)
				ma_sound_set_position(&shot, position->x, position->y, position->z);
			else ma_sound_set_spatialization_enabled(&shot, MA_FALSE);

			ma_sound_start(&shot);

			return true;
		};

		// A direction with no length has nothing to normalise.
		glm::vec3 Direction(const glm::vec3& vector, const glm::vec3& otherwise)
		{
			return glm::length(vector) > 0.0f ? glm::normalize(vector) : otherwise;
		};

		void Listen(ma_engine* engine)
		{
			glm::vec3 position(0.0f);
			glm::vec3 forward = FORWARD;
			glm::vec3 up = UP;

			if (const AudioListener* listener = AudioListener::Current())
			{
				const glm::mat4 world = listener->GetGameObject()->WorldMatrix();

				position = world[3];
				forward = Direction(-glm::vec3(world[2]), FORWARD);
				up = Direction(world[1], UP);
			};

			ma_engine_listener_set_position(engine, LISTENER, position.x, position.y, position.z);
			ma_engine_listener_set_direction(engine, LISTENER, forward.x, forward.y, forward.z);
			ma_engine_listener_set_world_up(engine, LISTENER, up.x, up.y, up.z);
		};
	};

	ma_engine* Audio::Get()
	{
		if (output || opening_failed)
			return output ? &output->engine : nullptr;

		Output* opened = new Output();

		ma_resource_manager_config resources = ma_resource_manager_config_init();
		resources.decodedFormat = ma_format_f32;
		resources.jobThreadCount = 0;
		resources.flags = MA_RESOURCE_MANAGER_FLAG_NO_THREADING;

		ma_result result = ma_resource_manager_init(&resources, &opened->resources);

		if (result == MA_SUCCESS)
		{
			ma_engine_config config = ma_engine_config_init();
			config.pResourceManager = &opened->resources;
			config.noDevice = useDevice ? MA_FALSE : MA_TRUE;

			if (!useDevice)
			{
				config.channels = SILENT_CHANNELS;
				config.sampleRate = SILENT_SAMPLE_RATE;
			};

			result = ma_engine_init(&config, &opened->engine);

			if (result != MA_SUCCESS)
				ma_resource_manager_uninit(&opened->resources);
		};

		if (result != MA_SUCCESS)
		{
			std::cerr << "Audio: no output: " << ma_result_description(result) << std::endl;

			delete opened;
			opening_failed = true;

			return nullptr;
		};

		output = opened;

		return &output->engine;
	};

	bool Audio::PlayAt(const std::string& path, const glm::vec3& position, float volume)
	{
		return Shoot(Get(), path, volume, &position);
	};

	bool Audio::Play(const std::string& path, float volume)
	{
		return Shoot(Get(), path, volume, nullptr);
	};

	void Audio::StopAll()
	{
		if (output)
			Reap(true);
	};

	void Audio::Unload()
	{
		if (!output)
			return;

		Reap(true);

		for (const std::string& path : output->registered)
			ma_resource_manager_unregister_file(&output->resources, path.c_str());

		output->registered.clear();
		output->failed.clear();
	};

	size_t Audio::PlayingCount()
	{
		if (!output)
			return 0;

		Reap(false);

		return output->shots.size();
	};

	void Audio::Tick()
	{
		if (!output)
			return;

		Reap(false);

		if (Engine::updateScenes)
			for (AudioSource* source : Sources())
				source->Hold();

		Listen(&output->engine);

		if (output->running == Engine::updateScenes)
			return;

		output->running = Engine::updateScenes;

		if (output->running)
			ma_engine_start(&output->engine);
		else ma_engine_stop(&output->engine);
	};

	void Audio::Mix(uint64_t frames)
	{
		// A device is already reading the mix on a thread of its own.
		if (!output || !output->running || ma_engine_get_device(&output->engine))
			return;

		std::vector<float> mixed(frames * ma_engine_get_channels(&output->engine));
		ma_engine_read_pcm_frames(&output->engine, mixed.data(), frames, nullptr);
	};

	AudioListener* AudioListener::Current()
	{
		for (Scene* scene : Scene::GetScenes())
			if (AudioListener* found = scene->GetRoot().FindComponent<AudioListener>())
				return found;

		return nullptr;
	};

	AudioSource::AudioSource()
	{
		Sources().push_back(this);
	};

	AudioSource::~AudioSource()
	{
		Release();
		std::erase(Sources(), this);
	};

	void AudioSource::OnUpdate()
	{
		m_updated = true;

		if (m_held)
		{
			m_held = false;
			ma_sound_start(m_sound);
		};

		if (!m_started && playOnStart)
			Play();

		m_started = true;

		Sync();
	};

	void AudioSource::Play()
	{
		m_started = true;
		m_held = false;

		ma_sound* sound = Load();

		if (!sound)
			return;

		Sync();
		ma_sound_seek_to_pcm_frame(sound, 0);
		ma_sound_start(sound);
	};

	void AudioSource::Stop()
	{
		m_held = false;

		if (!m_sound)
			return;

		ma_sound_stop(m_sound);
		ma_sound_seek_to_pcm_frame(m_sound, 0);
	};

	bool AudioSource::IsPlaying() const
	{
		return m_sound && ma_sound_is_playing(m_sound);
	};

	void AudioSource::Hold()
	{
		const bool updated = m_updated;
		m_updated = false;

		if (updated || !IsPlaying())
			return;

		ma_sound_stop(m_sound);
		m_held = true;
	};

	ma_sound* AudioSource::Load()
	{
		if (*clipPath == m_loaded_path)
			return m_sound;

		const bool playing = IsPlaying();

		Release();
		m_loaded_path = clipPath;

		ma_engine* engine = m_loaded_path.empty() ? nullptr : Audio::Get();

		if (!engine)
			return nullptr;

		ma_sound* sound = new ma_sound();
		const ma_result result = ma_sound_init_from_file(engine, m_loaded_path.c_str(), LOAD_FLAGS, nullptr, nullptr, sound);

		if (result != MA_SUCCESS)
		{
			std::cerr << "AudioSource: could not load " << m_loaded_path << ": " << ma_result_description(result) << std::endl;
			delete sound;

			return nullptr;
		};

		m_sound = sound;

		if (playing)
			ma_sound_start(m_sound);

		return m_sound;
	};

	void AudioSource::Sync()
	{
		ma_sound* sound = Load();

		if (!sound)
			return;

		const glm::vec3 position = m_gameObject->WorldMatrix()[3];

		ma_sound_set_volume(sound, volume);
		ma_sound_set_pitch(sound, (std::max)((float)pitch, MIN_PITCH));
		ma_sound_set_looping(sound, loop ? MA_TRUE : MA_FALSE);
		ma_sound_set_spatialization_enabled(sound, spatial ? MA_TRUE : MA_FALSE);
		ma_sound_set_min_distance(sound, minDistance);
		ma_sound_set_max_distance(sound, maxDistance);
		ma_sound_set_position(sound, position.x, position.y, position.z);
	};

	void AudioSource::Release()
	{
		m_held = false;

		if (!m_sound)
			return;

		ma_sound_uninit(m_sound);
		delete m_sound;
		m_sound = nullptr;
	};
};
