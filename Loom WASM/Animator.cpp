#include "Animator.h"

#include "GameObject.h"
#include "Skin.h"
#include "Utilities/Clock.h"

#include "glm/gtc/quaternion.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>


namespace Loom
{
	namespace
	{
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

			channel.property = *known;
			channel.keys = (size_t)keys[at];
			channel.first = at + 1;
			at = channel.first + channel.keys * (1 + ValueFloats(channel.property));

			if (at > keys.size())
				break;

			if (channel.property != Property::Weight)
			{
				channel.joint = std::find(m_jointNames.begin(), m_jointNames.end(), channel.target) - m_jointNames.begin();

				if (channel.joint == m_jointNames.size())
					m_jointNames.push_back(channel.target);
			};

			m_parsedChannels.push_back(channel);
		};
	};

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

	size_t Animator::PlayingIndex() const
	{
		return std::find_if(
			m_parsedClips.begin(),
			m_parsedClips.end(),
			[&](const Clip& each) { return each.name == m_playing; }) - m_parsedClips.begin();
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

	bool Animator::Play(const std::string& name)
	{
		Parse();

		const bool known = std::any_of(m_parsedClips.begin(), m_parsedClips.end(), [&](const Clip& each) { return each.name == name; });

		if (!known)
			return false;

		clip = name;
		m_playing = name;
		m_time = 0.0f;

		return true;
	};

	void Animator::OnUpdate()
	{
		Advance(Time::DeltaTime());
	};

	void Animator::Advance(float seconds)
	{
		if (*clip != m_playing)
		{
			m_playing = clip;
			m_time = 0.0f;
		};

		m_time += seconds * speed;

		if (switchAfter > 0.0f && m_time >= PlayFor())
		{
			m_time -= PlayFor();
			PlayNext();

			// A long frame moves on one clip, not several.
			m_time = std::fmod(m_time, PlayFor());
		};

		Pose(m_time);
	};

	float Animator::PlayFor()
	{
		const float duration = GetDuration();

		return duration > 0.0f ? std::ceil(switchAfter / duration) * duration : *switchAfter;
	};

	void Animator::PlayNext()
	{
		const std::vector<std::string> names = GetClipNames();

		if (names.empty())
			return;

		const size_t playing = std::find(names.begin(), names.end(), *clip) - names.begin();

		clip = names[playing < names.size() ? (playing + 1) % names.size() : 0];
		m_playing = clip;
	};

	void Animator::Pose(float time)
	{
		Parse();

		const size_t playing = PlayingIndex();

		if (playing == m_parsedClips.size())
			return;

		const float duration = m_parsedClips[playing].duration;

		if (duration <= 0.0f)
			time = 0.0f;
		else if (loop)
			time -= duration * std::floor(time / duration);
		else time = std::clamp(time, 0.0f, duration);

		const std::vector<GameObject*>& joints = m_joints.Find(m_gameObject, m_jointNames);
		const std::vector<Skin*>& skins = Skins();

		for (const Channel& channel : m_parsedChannels)
		{
			if (channel.clip != playing)
				continue;

			const std::vector<float> value = Sample(channel, time);

			if (value.empty())
				continue;

			if (channel.property == Property::Weight)
			{
				for (Skin* skin : skins)
					skin->SetShapeWeight(channel.target, value[0]);

				continue;
			};

			GameObject* joint = joints[channel.joint];

			if (joint == nullptr)
				continue;

			Transform& transform = joint->transform;

			switch (channel.property)
			{
			case Property::Translation:
				transform.position = glm::vec3(value[0], value[1], value[2]);
				break;

			case Property::Scale:
				transform.scale = glm::vec3(value[0], value[1], value[2]);
				break;

			case Property::Rotation:
				transform.rotation = Transform::EulerDegrees(glm::quat(value[3], value[0], value[1], value[2]));
				break;

			default:
				break;
			};
		};
	};

	std::vector<std::string> Animator::GetClipNames()
	{
		Parse();

		std::vector<std::string> names;

		for (const Clip& each : m_parsedClips)
			names.push_back(each.name);

		return names;
	};

	float Animator::GetDuration()
	{
		Parse();

		for (const Clip& each : m_parsedClips)
			if (each.name == *clip)
				return each.duration;

		return 0.0f;
	};

	void Animator::OnGui()
	{
		const std::vector<std::string> names = GetClipNames();

		if (names.empty())
			return;

		ImGui::SeparatorText("Clips");

		if (ImGui::BeginCombo("Clip", clip->c_str()))
		{
			for (const std::string& name : names)
				if (ImGui::Selectable(name.c_str(), name == *clip))
					clip = name;

			ImGui::EndCombo();
		};

		const float duration = GetDuration();
		const float into = duration <= 0.0f ? 0.0f : loop ? std::fmod(m_time, duration) : std::min(m_time, duration);

		ImGui::Text("%.2f / %.2f s", into, duration);
	};
};
