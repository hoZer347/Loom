#pragma once

#include "Loom API.h"

#include "Component.h"
#include "Skin.h"

#include "glm/gtc/quaternion.hpp"

#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::Animator
	* - Plays a model's animation clips on what is under its GameObject: a
	*   clip moves joints, found by name, through keyed positions, rotations
	*   and scales, and sets the weights of the Skins' shape keys
	* - Plays the clip at clip's place in m_clips, from its start whenever it
	*   changes, speed times as fast as the clock. With loop on it starts over
	*   at its end; off, it holds its last pose
	* - With switchAfter above 0 it moves on to the next clip, in order and
	*   round to the first again, once the playing one has played that many
	*   seconds, waiting for the end of its loop so no step is cut short
	* - A clip named "<from> to <to>" is a transition: going from the clip
	*   named from to the one named to, it plays that once on the way, then
	*   carries on into to. switchAfter passes over transitions
	* - Whenever the clip changes, the pose eases out of where the joints and
	*   shape keys were over blendTime seconds, so nothing snaps
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

		// Moves the playing clip on by seconds of the clock, switching clips as
		// switchAfter says, and puts the pose there on the joints and Skins.
		void Advance(float seconds);

		// How far into the playing clip it is.
		void OnGui() override;

		// The clips' names, for clip.
		std::vector<std::string> GetFieldChoices(const SerializedField& field) override;

		// Starts the named clip, or the clip at that place in the list, from its
		// beginning. False, changing nothing, for one it has no clip by.
		bool Play(const std::string& name);
		bool Play(int index);

		// Puts the playing clip's pose at time seconds into it on the joints and
		// Skins, wrapped or held by loop, a transition always held, without
		// moving the clock or easing out of an earlier pose.
		void Pose(float time);

		std::vector<std::string> GetClipNames();

		// The playing clip's name, or empty when clip is none of them.
		std::string GetClipName();

		// Of the playing clip, or 0 when clip is none of them.
		float GetDuration();

		float GetTime() const { return m_time; };

		// Which clip plays, by its place in m_clips. While a transition into it
		// plays, it already names the clip the transition leads to.
		Serial<int> clip = 0;
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

		// Seconds before moving on to the next clip, or 0 to stay on one. Last,
		// so the fields before it keep their places in scenes saved without it.
		Serial<float> switchAfter = 0.0f;

		// Seconds to ease from one clip's pose into the next, or 0 to cut.
		Serial<float> blendTime = 0.25f;

	private:
		struct Clip
		{
			std::string name;
			float duration = 0.0f;

			// For a transition, the clips it goes from and to; -1 otherwise.
			int from = -1;
			int to = -1;
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

		// The playing clip's index: the transition while one plays, otherwise
		// clip. The clip count when that is none of them.
		size_t PlayingIndex() const;

		// Starts playing the clip at index from its beginning, easing out of the
		// pose there is now, by way of the transition into it when there is one.
		void Start(int index);

		// The clip switchAfter moves on to: the next that is not a transition,
		// round to the first again.
		int NextIndex();

		// The transition from one clip to another, or -1 when there is none.
		int TransitionBetween(int from, int to);

		bool IsTransition(size_t index) const;

		// Remembers where the joints and shape keys are, to ease out of.
		void CaptureBlend();

		// Pose, eased out of the held pose while blendTime runs when blend is on.
		void Apply(float time, bool blend);

		// How long the playing clip plays before switchAfter moves on: the
		// whole loops that cover switchAfter, or switchAfter itself for a clip
		// with no length.
		float PlayFor();

		// The Skins under it, gathered again once anything has been added,
		// removed or moved.
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
		bool m_skinsGathered = false;
		unsigned long long m_skinsChanges = 0;

		// The clip m_time counts from the start of, or -1 before any has played.
		int m_playing = -1;
		float m_time = 0.0f;

		// The clip the playing transition leads into, or -1 when it is not one.
		// While one plays, clip already names where it leads.
		int m_after = -1;

		struct Held
		{
			glm::vec3 position{ 0.0f };
			glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			glm::vec3 scale{ 1.0f };
		};

		// Where each of m_jointNames was, and the weight of each weight
		// channel's shape key, when the clip last changed, and how much of
		// blendTime is left.
		std::vector<Held> m_heldJoints;
		std::vector<float> m_heldWeights;
		float m_blendLeft = 0.0f;
		float m_blendFor = 0.0f;
	};
};
