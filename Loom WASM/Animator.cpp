#include "Animator.h"

#include "GameObject.h"
#include "Skin.h"
#include "Utilities/Clock.h"

#include "glm/gtc/quaternion.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>


namespace Loom
{
	namespace
	{
		// What a transition's name puts between the clips it goes from and to.
		constexpr const char* TRANSITION_JOIN = " to ";

		constexpr Animator::Property PROPERTIES[] =
		{
			Animator::Property::Translation,
			Animator::Property::Rotation,
			Animator::Property::Scale,
			Animator::Property::Weight,
		};

		void CollectSkins(GameObject& object, std::vector<Skin*>& skins)
		{
			if (Skin* skin = object.GetComponent<Skin>())
				skins.push_back(skin);

			for (GameObject* child : object.GetChildren())
				CollectSkins(*child, skins);
		};
	};

	#pragma region Properties

	size_t Animator::ValueFloats(Property property)
	{
		constexpr size_t VECTOR = 3;
		constexpr size_t QUATERNION = 4;
		constexpr size_t SCALAR = 1;

		switch (property)
		{
		case Property::Rotation:	return QUATERNION;
		case Property::Weight:		return SCALAR;
		default:					return VECTOR;
		};
	};

	const char* Animator::PropertyName(Property property)
	{
		switch (property)
		{
		case Property::Translation:	return "translation";
		case Property::Rotation:	return "rotation";
		case Property::Scale:		return "scale";
		default:					return "weight";
		};
	};

	#pragma endregion

	#pragma region Playback

	void Animator::OnUpdate()
	{
		Advance(Time::DeltaTime());
	};

	void Animator::Advance(float seconds)
	{
		Parse();

		if (*clip != (m_after >= 0 ? m_after : m_playing))
			Start(clip);

		m_time += seconds * speed;
		m_blendLeft = std::max(0.0f, m_blendLeft - seconds * std::abs(speed));

		if (m_after >= 0 && m_time >= GetDuration())
		{
			// A transition ends where its clip begins, so it carries on into it
			// with no easing of its own.
			m_time -= GetDuration();
			m_playing = m_after;
			m_after = -1;
		};

		Apply(m_time, true);
	};

	bool Animator::Play(const std::string& name)
	{
		Parse();

		const auto found = std::find_if(m_parsedClips.begin(), m_parsedClips.end(), [&](const Clip& each) { return each.name == name; });

		return Play((int)(found - m_parsedClips.begin()));
	};

	bool Animator::Play(int index)
	{
		Parse();

		if (index < 0 || (size_t)index >= m_parsedClips.size())
			return false;

		Start(index);

		return true;
	};

	void Animator::Pose(float time)
	{
		Apply(time, false);
	};

	bool Animator::IsTransition(int index)
	{
		Parse();

		return index >= 0 && (size_t)index < m_parsedClips.size() && m_parsedClips[index].from >= 0;
	};

	size_t Animator::PlayingIndex() const
	{
		const int playing = m_after >= 0 ? m_playing : *clip;

		return playing >= 0 && (size_t)playing < m_parsedClips.size() ? (size_t)playing : m_parsedClips.size();
	};

	void Animator::Start(int index)
	{
		CaptureBlend();

		const int via = m_after < 0 ? TransitionBetween(m_playing, index) : -1;

		m_after = via >= 0 ? index : -1;
		m_playing = via >= 0 ? via : index;
		clip = index;
		m_time = 0.0f;
	};

	int Animator::TransitionBetween(int from, int to)
	{
		Parse();

		if (from < 0 || to < 0)
			return -1;

		for (size_t i = 0; i < m_parsedClips.size(); i++)
			if (m_parsedClips[i].from == from && m_parsedClips[i].to == to)
				return (int)i;

		return -1;
	};

	#pragma endregion

	#pragma region Clips

	std::vector<std::string> Animator::GetClipNames()
	{
		Parse();

		std::vector<std::string> names;

		for (const Clip& each : m_parsedClips)
			names.push_back(each.name);

		return names;
	};

	std::string Animator::GetClipName()
	{
		Parse();

		const size_t playing = PlayingIndex();

		return playing < m_parsedClips.size() ? m_parsedClips[playing].name : std::string();
	};

	float Animator::GetDuration()
	{
		Parse();

		const size_t playing = PlayingIndex();

		return playing < m_parsedClips.size() ? m_parsedClips[playing].duration : 0.0f;
	};

	#pragma endregion

	#pragma region Inspector

	void Animator::OnGui()
	{
		Parse();

		if (m_parsedClips.empty())
			return;

		const float duration = GetDuration();
		const float into = duration <= 0.0f ? 0.0f : loop ? std::fmod(m_time, duration) : std::min(m_time, duration);

		ImGui::Text("%.2f / %.2f s", into, duration);
	};

	std::vector<std::string> Animator::GetFieldChoices(const SerializedField& field)
	{
		if (field.data == &*clip)
			return GetClipNames();

		return { };
	};

	#pragma endregion

	#pragma region Parsing

	void Animator::Parse()
	{
		if (*m_clips == m_parsedClipText && *m_channels == m_parsedChannelText && m_keys->size() == m_parsedKeys)
			return;

		m_parsedClipText = m_clips;
		m_parsedChannelText = m_channels;
		m_parsedKeys = m_keys->size();
		m_parsedClips.clear();
		m_parsedChannels.clear();
		m_jointNames.clear();
		m_skinsGathered = false;

		for (const std::string& line : SplitLines(m_clips))
		{
			Clip& parsed = m_parsedClips.emplace_back();
			std::istringstream fields(line);
			std::getline(fields, parsed.name, '\t');
			fields >> parsed.duration;
		};

		// A transition's name is two other clips' names joined; any other name
		// with the join in it is just a name.
		const auto named = [&](const std::string& name)
		{
			for (size_t i = 0; i < m_parsedClips.size(); i++)
				if (m_parsedClips[i].name == name)
					return (int)i;

			return -1;
		};

		for (Clip& each : m_parsedClips)
			for (size_t join = each.name.find(TRANSITION_JOIN); join != std::string::npos; join = each.name.find(TRANSITION_JOIN, join + 1))
			{
				const int from = named(each.name.substr(0, join));
				const int to = named(each.name.substr(join + std::strlen(TRANSITION_JOIN)));

				if (from >= 0 && to >= 0)
				{
					each.from = from;
					each.to = to;
					break;
				};
			};

		const std::vector<float>& keys = *m_keys;
		size_t at = 0;

		for (const std::string& line : SplitLines(m_channels))
		{
			Channel channel;
			std::string property;
			std::istringstream fields(line);

			fields >> channel.clip;
			fields.ignore();
			std::getline(fields, property, '\t');
			std::getline(fields, channel.target);

			const auto known = std::find_if(
				std::begin(PROPERTIES),
				std::end(PROPERTIES),
				[&](Property each) { return property == PropertyName(each); });

			if (at >= keys.size() || known == std::end(PROPERTIES))
				break;

			const float count = keys[at];
			const size_t stride = 1 + ValueFloats(*known);

			// Not a count, or more keys than follow it.
			if (!(count >= 0.0f && count <= (float)keys.size()) || (size_t)count > (keys.size() - at - 1) / stride)
				break;

			channel.property = *known;
			channel.keys = (size_t)count;
			channel.first = at + 1;
			at = channel.first + channel.keys * stride;

			if (channel.property != Property::Weight)
			{
				channel.joint = std::find(m_jointNames.begin(), m_jointNames.end(), channel.target) - m_jointNames.begin();

				if (channel.joint == m_jointNames.size())
					m_jointNames.push_back(channel.target);
			};

			m_parsedChannels.push_back(channel);
		};
	};

	#pragma endregion

	#pragma region Posing

	std::vector<float> Animator::Sample(const Channel& channel, float time) const
	{
		const size_t floats = ValueFloats(channel.property);
		const size_t stride = 1 + floats;
		const float* key = m_keys->data() + channel.first;

		const auto value = [&](size_t index) { return key + index * stride + 1; };

		if (channel.keys == 0)
			return { };

		// The first key after time.
		size_t low = 0, high = channel.keys;

		while (low < high)
		{
			const size_t middle = (low + high) / 2;

			if (key[middle * stride] <= time)
				low = middle + 1;
			else high = middle;
		};

		if (low == 0)
			return { value(0), value(0) + floats };

		if (low == channel.keys)
			return { value(low - 1), value(low - 1) + floats };

		const float before = key[(low - 1) * stride];
		const float after = key[low * stride];
		const float blend = after > before ? (time - before) / (after - before) : 0.0f;

		const float* a = value(low - 1);
		const float* b = value(low);

		if (channel.property == Property::Rotation)
		{
			const glm::quat turned = glm::slerp(glm::quat(a[3], a[0], a[1], a[2]), glm::quat(b[3], b[0], b[1], b[2]), blend);
			return { turned.x, turned.y, turned.z, turned.w };
		};

		std::vector<float> blended(floats);

		for (size_t i = 0; i < floats; i++)
			blended[i] = a[i] + (b[i] - a[i]) * blend;

		return blended;
	};

	void Animator::CaptureBlend()
	{
		m_blendLeft = 0.0f;

		if (blendTime <= 0.0f || m_playing < 0)
			return;

		const std::vector<GameObject*>& joints = m_joints.Find(m_gameObject, m_jointNames);

		m_heldJoints.assign(joints.size(), Held());

		for (size_t i = 0; i < joints.size(); i++)
			if (joints[i])
			{
				const Transform& transform = joints[i]->transform;
				m_heldJoints[i] = { *transform.position, Transform::Rotation(*transform.rotation), *transform.scale };
			};

		const std::vector<Skin*>& skins = Skins();

		m_heldWeights.assign(m_parsedChannels.size(), 0.0f);

		if (!skins.empty())
			for (size_t i = 0; i < m_parsedChannels.size(); i++)
				if (m_parsedChannels[i].property == Property::Weight)
					m_heldWeights[i] = skins.front()->GetShapeWeight(m_parsedChannels[i].target);

		m_blendLeft = blendTime;
		m_blendFor = blendTime;
	};

	void Animator::Apply(float time, bool blend)
	{
		Parse();

		const size_t playing = PlayingIndex();

		if (playing == m_parsedClips.size())
			return;

		const float duration = m_parsedClips[playing].duration;

		if (duration <= 0.0f)
			time = 0.0f;
		else if (loop && !IsTransition((int)playing))
			time -= duration * std::floor(time / duration);
		else time = std::clamp(time, 0.0f, duration);

		const std::vector<GameObject*>& joints = m_joints.Find(m_gameObject, m_jointNames);
		const std::vector<Skin*>& skins = Skins();

		// How far out of the held pose, eased in and out.
		const bool blending = blend && m_blendLeft > 0.0f &&
			m_heldJoints.size() == joints.size() && m_heldWeights.size() == m_parsedChannels.size();
		const float eased = blending ? glm::smoothstep(0.0f, 1.0f, 1.0f - m_blendLeft / m_blendFor) : 1.0f;

		for (size_t i = 0; i < m_parsedChannels.size(); i++)
		{
			const Channel& channel = m_parsedChannels[i];

			if (channel.clip != playing)
				continue;

			const std::vector<float> value = Sample(channel, time);

			if (value.empty())
				continue;

			if (channel.property == Property::Weight)
			{
				const float weight = blending ? glm::mix(m_heldWeights[i], value[0], eased) : value[0];

				for (Skin* skin : skins)
					skin->SetShapeWeight(channel.target, weight);

				continue;
			};

			GameObject* joint = joints[channel.joint];

			if (joint == nullptr)
				continue;

			Transform& transform = joint->transform;
			const Held* held = blending ? &m_heldJoints[channel.joint] : nullptr;

			switch (channel.property)
			{
			case Property::Translation:
			{
				const glm::vec3 position(value[0], value[1], value[2]);
				transform.position = held ? glm::mix(held->position, position, eased) : position;
			}
			break;

			case Property::Scale:
			{
				const glm::vec3 scale(value[0], value[1], value[2]);
				transform.scale = held ? glm::mix(held->scale, scale, eased) : scale;
			}
			break;

			case Property::Rotation:
			{
				const glm::quat rotation(value[3], value[0], value[1], value[2]);
				transform.rotation = Transform::EulerDegrees(held ? glm::slerp(held->rotation, rotation, eased) : rotation);
			}
			break;

			default:
				break;
			};
		};
	};

	const std::vector<Skin*>& Animator::Skins()
	{
		if (m_skinsGathered && GameObject::HierarchyChanges() == m_skinsChanges)
			return m_skins;

		m_skins.clear();
		CollectSkins(*m_gameObject, m_skins);

		m_skinsGathered = true;
		m_skinsChanges = GameObject::HierarchyChanges();

		return m_skins;
	};

	#pragma endregion
};
