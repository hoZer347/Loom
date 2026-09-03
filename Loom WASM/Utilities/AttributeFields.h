#pragma once

#include "Attributes.h"

#include <initializer_list>
#include <string>
#include <typeinfo>
#include <vector>


namespace Loom
{
	/// One attribute found on an object, under the name of the field holding it.
	struct AttributeField final
	{
		std::string Name;
		AttributeBase* Attribute = nullptr;
	};

	/// Something that owns attributes and can list them.
	///
	/// Declared once with HOZER_ATTRIBUTES beside the fields themselves, so the list
	/// cannot drift from them. A separately maintained array goes stale silently the
	/// first time somebody adds an attribute and forgets it.
	///
	/// C++ has no reflection to lean on, so the declaration is made once, next to the
	/// fields themselves, with HOZER_ATTRIBUTES. That is still one list rather than two:
	/// the names come from the preprocessor stringifying the very expressions that
	/// produce the pointers, so a name and its field cannot drift apart, and a renamed
	/// field is a compile error rather than a readout that quietly says nothing.
	///
	/// The list is built once per object, on first use, and held for the object's life.
	struct AttributeOwner
	{
		virtual ~AttributeOwner() = default;

		/// Every attribute on this object, in declaration order.
		const std::vector<AttributeField>& Attributes() const
		{
			if (!m_built)
			{
				// The fields are the owner's own, and something reading them -- the GUI,
				// a copy, a save -- writes to them. Collecting is the one const step that
				// has to hand out mutable pointers, so it is the one that casts.
				const_cast<AttributeOwner*>(this)->CollectAttributes(m_fields);

				m_built = true;
			};

			return m_fields;
		};

		/// The attribute declared under that field name, or null.
		AttributeBase* AttributeNamed(const std::string& name) const
		{
			for (const AttributeField& field : Attributes())
				if (field.Name == name)
					return field.Attribute;

			return nullptr;
		};

		/// Drops the built list, so the next read collects again. For a test, or for an
		/// object whose attributes are held indirectly and have been swapped out.
		void ForgetAttributes() const
		{
			m_fields.clear();
			m_built = false;
		};

	protected:
		virtual void CollectAttributes(std::vector<AttributeField>& into) = 0;

	private:
		mutable std::vector<AttributeField> m_fields{ };
		mutable bool m_built = false;
	};

	struct AttributeFields final
	{
		/// Every attribute on an owner, in declaration order.
		static const std::vector<AttributeField>& Of(const AttributeOwner& owner)
		{
			return owner.Attributes();
		};

		/// Copies every numeric attribute's base from one object onto another of the same type.
		///
		/// Field by field in declaration order, which is safe precisely because both sides
		/// are the same type. Names are checked anyway: a silent mis-pairing here would
		/// copy Poison into Bleed and be very hard to see.
		///
		/// Only the base is carried. Should modifiers ever start being pushed rather than
		/// asked for at read time, this becomes a partial copy and whatever relies on it --
		/// a preview, a simulation run against a copy -- starts quietly lying, so it is
		/// worth knowing.
		static void CopyBases(const AttributeOwner& from, AttributeOwner& to)
		{
			if (typeid(from) != typeid(to))
				return;

			const std::vector<AttributeField>& source = from.Attributes();
			const std::vector<AttributeField>& target = to.Attributes();

			for (size_t i = 0; i < source.size() && i < target.size(); i++)
			{
				if (source[i].Name != target[i].Name
					|| source[i].Attribute == nullptr
					|| target[i].Attribute == nullptr)
					continue;

				double baseValue = 0.0;
				double value = 0.0;

				if (source[i].Attribute->TryGetNumbers(baseValue, value))
					target[i].Attribute->TrySetBaseNumber(baseValue);
			};
		};

		/// Pairs the stringified argument list with the pointers it produced. Called by
		/// HOZER_ATTRIBUTES; not meant to be used directly.
		static void Zip(
			std::vector<AttributeField>& into,
			const char* names,
			std::initializer_list<AttributeBase*> attributes);

		AttributeFields() = delete;
	};
};


/// Declares an object's attributes, inside a struct deriving AttributeOwner:
///
///		struct Fighter : Loom::AttributeOwner
///		{
///			Loom::Attribute<int> health{ 10 };
///			Loom::Attribute<int> attack{ 2 };
///
///			HOZER_ATTRIBUTES(&health, &attack);
///		};
///
/// The names come from the preprocessor, so they always match the fields.
#define HOZER_ATTRIBUTES(...)																\
	void CollectAttributes(std::vector<Loom::AttributeField>& into) override			\
	{																						\
		Loom::AttributeFields::Zip(into, #__VA_ARGS__, { __VA_ARGS__ });				\
	}
