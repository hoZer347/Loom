#pragma once

#include "Loom API.h"

#include "SerializedField.h"

#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"

#include <initializer_list>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>


namespace Loom
{
	struct LoomObject;

	// Hands a field to the last LoomObject whose constructor started on this
	// thread. A Serial member is built as part of its owner, before that
	// constructor has finished, which is what makes that object the right one.
	LOOM_API void RegisterSerialField(
		FieldType type,
		void* data,
		LoomObject* (*get_reference)(void*),
		void (*set_reference)(void*, LoomObject*),
		bool (*accepts_reference)(LoomObject*));

	// Every type the scene format can write, named by the member's own type.
	// Anything else is a compile error rather than a field that quietly writes
	// the wrong bytes. An enum is written as its underlying integer, and the
	// editor offers its enumerators as a dropdown.
	template <typename T>
	constexpr FieldType FieldTypeOf()
	{
		if constexpr (std::is_enum_v<T>)							return FieldTypeOf<std::underlying_type_t<T>>();
		else if constexpr (std::is_same_v<T, bool>)					return FieldType::Bool;
		else if constexpr (std::is_same_v<T, int>)					return FieldType::Int;
		else if constexpr (std::is_same_v<T, unsigned int>)			return FieldType::UInt;
		else if constexpr (std::is_same_v<T, long long>)			return FieldType::Int64;
		else if constexpr (std::is_same_v<T, unsigned long long>)	return FieldType::UInt64;
		else if constexpr (std::is_same_v<T, float>)				return FieldType::Float;
		else if constexpr (std::is_same_v<T, double>)				return FieldType::Double;
		else if constexpr (std::is_same_v<T, std::string>)			return FieldType::String;
		else if constexpr (std::is_same_v<T, glm::vec2>)			return FieldType::Vec2;
		else if constexpr (std::is_same_v<T, glm::vec3>)			return FieldType::Vec3;
		else if constexpr (std::is_same_v<T, glm::vec4>)			return FieldType::Vec4;
		else if constexpr (std::is_same_v<T, std::vector<float>>)	return FieldType::FloatArray;
		else if constexpr (std::is_pointer_v<T>)					return FieldType::Reference;
		else static_assert(sizeof(T) == 0, "Serial<T> has no scene-format type for this member");
	};

	/**
	* Loom::Serial
	* - A member of a LoomObject that the scene format writes and the editor's
	*   inspector draws
	* - Declaring one is the whole declaration: it registers itself with the
	*   object under construction on this thread and behaves as a T from then on,
	*   through an implicit conversion for the value and -> for its members
	* - The editor labels it after the member: Serial<float> m_speed reads
	*   "Speed" in the inspector
	* - A field is identified by where it was declared, so the scene format keys
	*   it by position: reordering the Serial members of a type reassigns the
	*   values in scenes already saved, and inserting one in the middle shifts
	*   everything after it
	* - Which is what it asks in return: a Serial has to be a member of the
	*   LoomObject being built around it. One declared anywhere else - a local, a
	*   global, a member of something that is not a LoomObject - attaches to
	*   whatever object was built last and leaves a field pointing into freed
	*   memory behind, and a LoomObject constructed inside another one's member
	*   initialisers takes the fields declared after it with it
	*/
	template <typename T>
	struct Serial final
	{
		// Declared bare, or with the default the member would have had anyway:
		// Serial<float> m_speed = 0.02f.
		Serial() : m_value() { Register(); };

		// Anything T itself would take, so a string field can be given a literal
		// the way the plain member it replaces was.
		template <typename U>
			requires (
				!std::is_same_v<std::remove_cvref_t<U>, Serial> &&
				std::is_constructible_v<T, U&&>)
		Serial(U&& value) : m_value(std::forward<U>(value)) { Register(); };

		// A field points into the object that declared it, and objects are
		// identities rather than values.
		Serial(const Serial&) = delete;
		Serial& operator=(const Serial&) = delete;

		// Assigned like the plain member it replaces, in three overloads because
		// one cannot cover it. This one takes anything T can be assigned from
		// without building a T first, which is what keeps it from tying with the
		// deleted copy-assignment above.
		template <typename U>
			requires (
				!std::is_same_v<std::remove_cvref_t<U>, Serial> &&
				std::is_assignable_v<T&, U&&>)
		Serial& operator=(U&& value) { m_value = std::forward<U>(value); return *this; };

		// A braced list deduces nothing above, and choosing between building a T
		// out of it and building a Serial out of it is a tie the compiler will
		// not break; taking the list itself is neither.
		template <typename U>
			requires std::is_assignable_v<T&, std::initializer_list<U>>
		Serial& operator=(std::initializer_list<U> values) { m_value = values; return *this; };

		// And what is left: = { } and a braced list for an aggregate, neither of
		// which deduces a U at all.
		Serial& operator=(T value) { m_value = std::move(value); return *this; };

		operator T& () { return m_value; };
		operator const T& () const { return m_value; };

		T& operator*() { return m_value; };
		const T& operator*() const { return m_value; };

		// A Serial holding a pointer hands the pointer over, so a reference
		// field reaches what it points at rather than the field itself.
		auto operator->()
		{
			if constexpr (std::is_pointer_v<T>)
				return m_value;
			else return &m_value;
		};

		auto operator->() const
		{
			if constexpr (std::is_pointer_v<T>)
				return m_value;
			else return &m_value;
		};

	private:
		void Register()
		{
			if constexpr (std::is_pointer_v<T>)
			{
				static_assert(
					std::is_base_of_v<LoomObject, std::remove_pointer_t<T>>,
					"Only pointers to LoomObjects can be serialized as references");

				// Generated here, where T is still known: the LoomObject part of
				// a T is not necessarily at the front of it, so the field cannot
				// do these casts through a LoomObject** of its own.
				RegisterSerialField(
					FieldType::Reference,
					&m_value,
					[](void* data) -> LoomObject*
					{
						return *(T*)data;
					},
					[](void* data, LoomObject* object)
					{
						*(T*)data = dynamic_cast<T>(object);
					},
					[](LoomObject* object)
					{
						return dynamic_cast<T>(object) != nullptr;
					});
			}
			else RegisterSerialField(FieldTypeOf<T>(), Data(), nullptr, nullptr, nullptr);
		};

		// The vector types are written out of their components, so the field
		// points at those rather than at the struct around them.
		void* Data()
		{
			constexpr FieldType type = FieldTypeOf<T>();

			if constexpr (
				type == FieldType::Vec2 ||
				type == FieldType::Vec3 ||
				type == FieldType::Vec4)
				return glm::value_ptr(m_value);
			else return &m_value;
		};

		T m_value;
	};
};
