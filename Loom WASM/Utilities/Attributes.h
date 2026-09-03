#pragma once

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>


namespace Loom
{
	/// A stackable change to an attribute's value. Untyped base so an attribute can hold
	/// a list of them without knowing what it is an attribute of.
	struct Modifier
	{
		virtual ~Modifier() = default;
	};

	template <typename _ValueType>
	struct TypedModifier : Modifier
	{
		virtual void Apply(_ValueType& value) const = 0;
	};

	/// Something that runs whenever the attribute it is attached to is invalidated.
	struct AttributeTrigger
	{
		virtual ~AttributeTrigger() = default;

		virtual void Execute() = 0;
	};

	/// When the value is recomputed: OnModifier caches until something invalidates it,
	/// OnValue recomputes on every read. Tag types, so the choice is part of the
	/// attribute's type rather than a runtime flag.
	struct OnModifier { };
	struct OnValue { };

	/// Handle returned by AttributeBase::Subscribe. Unsubscribes when it dies, so a
	/// listener cannot leave a dangling callback behind it.
	struct Subscription final
	{
		Subscription() = default;
		Subscription(std::function<void()> release) : m_release(std::move(release)) { };

		Subscription(const Subscription&) = delete;
		Subscription& operator=(const Subscription&) = delete;

		Subscription(Subscription&& other) noexcept : m_release(std::move(other.m_release))
		{
			other.m_release = nullptr;
		};

		Subscription& operator=(Subscription&& other) noexcept
		{
			if (this != &other)
			{
				Release();
				m_release = std::move(other.m_release);
				other.m_release = nullptr;
			};

			return *this;
		};

		~Subscription() { Release(); };

		void Release()
		{
			if (m_release)
			{
				m_release();
				m_release = nullptr;
			};
		};

	private:
		std::function<void()> m_release;
	};

	/// The untyped face of an attribute -- what a readout, a save or a copy can work with
	/// without knowing the value type.
	struct AttributeBase
	{
		virtual ~AttributeBase() = default;

		/// The current value rendered for display. Empty for a type with no sensible text.
		virtual std::string GetValueString() const { return ""; };

		/// Recompute on the next read, and tell everyone watching.
		virtual void Invalidate() { };

		/// Raised whenever the attribute is invalidated -- a base written, a modifier
		/// added, removed or edited. A readout subscribes to this instead of re-reading
		/// the value every frame; nothing here needs a poll to notice a change.
		[[nodiscard]] Subscription Subscribe(std::function<void()> onChanged)
		{
			const size_t id = m_next_listener++;

			m_listeners.emplace_back(id, std::move(onChanged));

			return Subscription(
				[this, id]()
				{
					for (size_t i = 0; i < m_listeners.size(); i++)
						if (m_listeners[i].first == id)
						{
							m_listeners.erase(m_listeners.begin() + i);
							return;
						};
				});
		};

		/// The value as a number, when the value type is one. False for the types that
		/// are not -- a string attribute has no delta to draw -- so a caller can skip
		/// rather than guess at a zero.
		virtual bool TryGetNumbers(double& baseValue, double& value) const
		{
			baseValue = 0.0;
			value = 0.0;

			return false;
		};

		/// Writes the base from a number, for a value type that is one. The mirror of
		/// TryGetNumbers, and it exists for the same reason: something that has to walk
		/// every attribute an object has -- a save, a diff, a copy taken to simulate
		/// against -- cannot know each one's type.
		virtual bool TrySetBaseNumber(double value) { return false; };

		/// How many modifiers are stacked on it, for a readout that lists them.
		virtual size_t ModifierCount() const { return 0; };

	protected:
		void RaiseChanged()
		{
			// Copied, because a listener is allowed to unsubscribe from inside the call.
			const auto listeners = m_listeners;

			for (const auto& listener : listeners)
				if (listener.second)
					listener.second();
		};

	private:
		std::vector<std::pair<size_t, std::function<void()>>> m_listeners{ };
		size_t m_next_listener = 0;
	};

	/// A value with a base, a stack of modifiers over it, and triggers that fire when it
	/// changes. _CacheMode is OnModifier (cache, recompute when invalidated) or OnValue
	/// (recompute on every read).
	template <typename _ValueType, typename _CacheMode = OnModifier>
	struct Attribute : AttributeBase
	{
		static_assert(
			std::is_same_v<_CacheMode, OnModifier> || std::is_same_v<_CacheMode, OnValue>,
			"Cache mode must be OnModifier or OnValue");

		using ValueType = _ValueType;
		using OnModify = std::function<void()>;

		Attribute() = default;

		Attribute(OnModify onModify) : onModify(std::move(onModify)) { };

		Attribute(const _ValueType& base) : m_base(base) { };

		Attribute(const _ValueType& base, OnModify onModify) :
			m_base(base),
			onModify(std::move(onModify))
		{ };

		const _ValueType& Base() const { return m_base; };

		_ValueType Value() const
		{
			if constexpr (std::is_same_v<_CacheMode, OnValue>)
				return Recalculate();
			else
			{
				if (dirty)
				{
					cachedValue = Recalculate();
					dirty = false;
				};

				return cachedValue;
			};
		};

		void SetBase(const _ValueType& value)
		{
			m_base = value;

			Invalidate();
		};

		/// Takes ownership. The returned pointer stays valid until it is removed or the
		/// attribute dies, and is what a caller keeps in order to remove the modifier
		/// later.
		TypedModifier<_ValueType>* AddModifier(std::unique_ptr<TypedModifier<_ValueType>> modifier)
		{
			TypedModifier<_ValueType>* raw = modifier.get();

			modifiers.emplace_back(std::move(modifier));

			Invalidate();

			return raw;
		};

		/// Builds the modifier in place: attribute.AddModifier<Md_AddInt>(5).
		template <typename _Modifier, typename... _Args>
		_Modifier* AddModifier(_Args&&... args)
		{
			static_assert(
				std::is_base_of_v<TypedModifier<_ValueType>, _Modifier>,
				"Modifier must modify this attribute's value type");

			auto owned = std::make_unique<_Modifier>(std::forward<_Args>(args)...);
			_Modifier* raw = owned.get();

			modifiers.emplace_back(std::move(owned));

			Invalidate();

			return raw;
		};

		void RmvModifier(const TypedModifier<_ValueType>* modifier)
		{
			for (size_t i = 0; i < modifiers.size(); i++)
				if (modifiers[i].get() == modifier)
				{
					modifiers.erase(modifiers.begin() + i);
					break;
				};

			Invalidate();
		};

		void ClearModifiers()
		{
			modifiers.clear();

			Invalidate();
		};

		AttributeTrigger* AddTrigger(std::unique_ptr<AttributeTrigger> trigger)
		{
			AttributeTrigger* raw = trigger.get();

			triggers.emplace_back(std::move(trigger));

			return raw;
		};

		template <typename _Trigger, typename... _Args>
		_Trigger* AddTrigger(_Args&&... args)
		{
			static_assert(
				std::is_base_of_v<AttributeTrigger, _Trigger>,
				"Trigger must derive from AttributeTrigger");

			auto owned = std::make_unique<_Trigger>(std::forward<_Args>(args)...);
			_Trigger* raw = owned.get();

			triggers.emplace_back(std::move(owned));

			return raw;
		};

		void RmvTrigger(const AttributeTrigger* trigger)
		{
			for (size_t i = 0; i < triggers.size(); i++)
				if (triggers[i].get() == trigger)
				{
					triggers.erase(triggers.begin() + i);
					return;
				};
		};

		size_t ModifierCount() const override { return modifiers.size(); };

		std::string GetValueString() const override
		{
			if constexpr (std::is_same_v<_ValueType, std::string>)
				return Value();
			else if constexpr (std::is_arithmetic_v<_ValueType>)
				return std::to_string(Value());
			else return "";
		};

		void Invalidate() override
		{
			dirty = true;

			if (onModify)
				onModify();

			for (const auto& trigger : triggers)
				if (trigger)
					trigger->Execute();

			RaiseChanged();
		};

		bool TryGetNumbers(double& baseValue, double& value) const override
		{
			baseValue = 0.0;
			value = 0.0;

			// bool and char convert, but neither is meaningfully a number here -- a
			// readout drawing a delta between two bools is noise, not information.
			if constexpr (
				std::is_arithmetic_v<_ValueType>
				&& !std::is_same_v<_ValueType, bool>
				&& !std::is_same_v<_ValueType, char>)
			{
				baseValue = double(m_base);
				value = double(Value());

				return true;
			}
			else return false;
		};

		bool TrySetBaseNumber(double value) override
		{
			if constexpr (
				std::is_arithmetic_v<_ValueType>
				&& !std::is_same_v<_ValueType, bool>
				&& !std::is_same_v<_ValueType, char>)
			{
				SetBase(_ValueType(value));

				return true;
			}
			else return false;
		};

	private:
		_ValueType Recalculate() const
		{
			_ValueType value = m_base;

			for (const auto& modifier : modifiers)
				if (const auto* typed = dynamic_cast<const TypedModifier<_ValueType>*>(modifier.get()))
					typed->Apply(value);

			return value;
		};

		_ValueType m_base{ };

		OnModify onModify{ };

		// Mutable so Value() can stay const -- the cache is not part of the value.
		mutable _ValueType cachedValue{ };
		mutable bool dirty = true;

		std::vector<std::unique_ptr<Modifier>> modifiers{ };
		std::vector<std::unique_ptr<AttributeTrigger>> triggers{ };
	};
};
