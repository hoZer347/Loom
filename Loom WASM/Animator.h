#pragma once

#include "Loom API.h"

#include "Component.h"
#include "Skin.h"

#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::Animator
	* - Plays a model's animation clips on what is under its GameObject: a
	*   clip moves joints, found by name, through keyed positions, rotations
	*   and scales, and sets the weights of the Skins' shape keys
	* - Plays the clip named by clip, from its start whenever the name
	*   changes, speed times as fast as the clock. With loop on it starts over
	*   at its end; off, it holds its last pose
	* - Between keys, positions, scales and weights are blended straight and
	*   rotations along the shortest arc
	* - What a clip has no keys for is left where it is
	*/
	struct LOOM_API Animator : Component<Animator, "Animation">
	{
		enum class Property { Translation, Rotation, Scale, Weight };

		// The floats a key of each property holds after its time: a position
		// or scale, a rotation as a quaternion's x, y, z and w, or a weight.
		static size_t ValueFloats(Property property);

		// The name m_channels gives each property.
		static const char* PropertyName(Property property);

		void OnUpdate() override;

		// A choice of the clips, and how far into the playing one it is.
		void OnGui() override;

		// Starts the named clip from its beginning. False, changing nothing,
		// for a name it has no clip by.
		bool Play(const std::string& name);

		// Puts the playing clip's pose at time seconds into it on the joints and
		// Skins, wrapped or held by loop, without moving the clock.
		void Pose(float time);

		std::vector<std::string> GetClipNames();

		// Of the clip named by clip, or 0 when there is none by that name.
		float GetDuration();

		float GetTime() const { return m_time; };

		Serial<std::string> clip;
		Serial<float> speed = 1.0f;
		Serial<bool> loop = true;

		// One clip a line: its name, a tab, and its length in seconds.
		Serial<std::string> m_clips;

		// One channel a line, by tabs: the index of its clip, its property
		// (translation, rotation, scale or weight) and what it moves, a joint or
		// a shape key, by name.
		Serial<std::string> m_channels;

		// For each channel in turn, how many keys it has, then each key: its
		// time in seconds and then its value.
		Serial<std::vector<float>> m_keys;

	private:
		struct Clip
		{
			std::string name;
			float duration = 0.0f;
		};

		struct Channel
		{
			size_t clip = 0;
			Property property = Property::Translation;
			std::string target;

			// Its joint's place in m_jointNames, for all but a weight.
			size_t joint = 0;

			// Where its first key's time is in m_keys, and how many keys follow.
			size_t first = 0;
			size_t keys = 0;
		};

		// Reads the clips and channels again when their text has changed.
		void Parse();

		// The value of a channel at time, its keys either side blended.
		std::vector<float> Sample(const Channel& channel, float time) const;

		// The playing clip's index, or the clip count when none plays.
		size_t PlayingIndex() const;

		// The Skins under it, gathered again once one of them is gone.
		const std::vector<Skin*>& Skins();

		std::vector<Clip> m_parsedClips;
		std::vector<Channel> m_parsedChannels;

		// What was parsed, to tell when it has changed.
		std::string m_parsedClipText;
		std::string m_parsedChannelText;
		size_t m_parsedKeys = 0;

		// The joints the channels move, each once.
		std::vector<std::string> m_jointNames;
		NamedObjects m_joints;

		std::vector<Skin*> m_skins;
		std::vector<Guid> m_skinGuids;
		bool m_skinsGathered = false;

		// The clip m_time counts from the start of.
		std::string m_playing;
		float m_time = 0.0f;
	};
};
