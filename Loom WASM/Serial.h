#pragma once

#include "Loom API.h"
#include "Macro Helpers.h"

#include "Asset.h"
#include "SerializedField.h"

#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"

#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>


namespace Loom
{
	struct LoomObject;
	struct StateReference;
	struct UniformValues;

	/**
	* Loom::MultilineText
	* - A string the inspector edits in a box of several lines rather than
	*   one: Serial<MultilineText> m_message = "Hello"
	* - Written to scenes as the string it is, so a Serial<std::string> that
	*   becomes one keeps the values already saved
	*/
	struct MultilineText final : std::string
	{
		using std::string::string;

		MultilineText() = default;
		MultilineText(std::string text) : std::string(std::move(text)) { };
	};

	#pragma region Registration

	// Hands a field to the last LoomObject whose constructor started on this
	// thread, and returns that object. A Serial member is built as part of its
	// owner, before that constructor has finished, which is what makes that
	// object the right one.
	LOOM_API LoomObject* RegisterSerialField(
		FieldType type,
		void* data,
		LoomObject* (*get_reference)(void*),
		void (*set_reference)(void*, LoomObject*),
		bool (*accepts_reference)(LoomObject*),
		const char* name,
		unsigned long long* version,
		std::span<const char* const> extensions = { });

	// After a field of owner changed, for the journal of what changed and,
	// for a reference, the index of who points where.
	LOOM_API void SerialChanged(LoomObject* owner, bool reference);

	// Hands a button to the same object, with its name and what pressing it runs.
	LOOM_API void RegisterButton(const char* name, const std::function<void()>* press);

	// The object the next field, button or other member that registers itself
	// belongs to, or none when no LoomObject has been built on this thread.
	LOOM_API LoomObject* ObjectUnderConstruction();

	#pragma endregion

	/**
	* Loom::Button
	* - A button the inspector draws among a LoomObject's fields, running its
	*   function when it is pressed:
	*   Button<"Map/Regrow"> m_regrow{ [this] { Regrow(); } }
	* - Named as a Serial is, categories and ".../" included, and drawn in its
	*   category in the order it was declared among the fields
	* - Not a field: the scene format never sees it
	* - A member of the LoomObject being built around it, as a Serial is, and
	*   only that
	*/
	template <Str name>
	struct Button final
	{
		explicit Button(std::function<void()> press) : m_press(std::move(press)) { RegisterButton(name.value, &m_press); };

		Button(const Button&) = delete;
		Button& operator=(const Button&) = delete;

	private:
		std::function<void()> m_press;
	};

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
		else if constexpr (std::is_same_v<T, MultilineText>)		return FieldType::MultilineText;
		else if constexpr (requires { T::fileExtensions; })			return FieldType::Asset;
		else if constexpr (std::is_same_v<T, glm::vec2>)			return FieldType::Vec2;
		else if constexpr (std::is_same_v<T, glm::vec3>)			return FieldType::Vec3;
		else if constexpr (std::is_same_v<T, glm::vec4>)			return FieldType::Vec4;
		else if constexpr (std::is_same_v<T, std::vector<float>>)	return FieldType::FloatArray;
		else if constexpr (std::is_same_v<T, std::vector<unsigned int>>)	return FieldType::UIntArray;
		else if constexpr (std::is_same_v<T, UniformValues>)		return FieldType::Uniforms;
		else if constexpr (std::is_same_v<T, StateReference>)		return FieldType::State;
		else if constexpr (std::is_pointer_v<T>)					return FieldType::Reference;
		else static_assert(sizeof(T) == 0, "Serial<T> has no scene-format type for this member");
	};

	/**
	* Loom::Serial
	* - A member of a LoomObject that the scene format writes and the editor's
	*   inspector draws
	* - Declaring one is the whole declaration: it registers itself with the
	*   object under construction on this thread and behaves as a T from then on,
	*   through an implicit conversion for reading the value, assignment and
	*   the arithmetic ones for changing it, and * and -> for reaching into it
	* - The editor labels it after the member: Serial<float> m_speed reads
	*   "Speed" in the inspector. Given a name, it reads that instead, and a
	*   category before the name, with '/' between nested ones, groups it with
	*   the others of that category, folding open and shut:
	*   Serial<float, "Movement/Top Speed"> m_speed. ".../" in front of a name
	*   stands for the category of the field or Button declared before it, so
	*   Serial<float, ".../Turn Speed"> after that one is "Movement/Turn Speed".
	*   The name is not written to the scene, so naming a field leaves the
	*   values already saved as they were
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
	template <typename T, Str name = "">
	struct Serial final
	{
		#pragma region Construction

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

		#pragma endregion

		#pragma region Assignment

		// Assigned like the plain member it replaces, in three overloads because
		// one cannot cover it. This one takes anything T can be assigned from
		// without building a T first, which is what keeps it from tying with the
		// deleted copy-assignment above.
		template <typename U>
			requires (
				!std::is_same_v<std::remove_cvref_t<U>, Serial> &&
				std::is_assignable_v<T&, U&&>)
		Serial& operator=(U&& value) { m_value = std::forward<U>(value); Changed(); return *this; };

		// A braced list deduces nothing above, and choosing between building a T
		// out of it and building a Serial out of it is a tie the compiler will
		// not break; taking the list itself is neither.
		template <typename U>
			requires std::is_assignable_v<T&, std::initializer_list<U>>
		Serial& operator=(std::initializer_list<U> values) { m_value = values; Changed(); return *this; };

		// And what is left: = { } and a braced list for an aggregate, neither of
		// which deduces a U at all.
		Serial& operator=(T value) { m_value = std::move(value); Changed(); return *this; };

		template <typename U> requires requires (T& value, U&& by) { value += std::forward<U>(by); }
		Serial& operator+=(U&& by) { m_value += std::forward<U>(by); Changed(); return *this; };

		template <typename U> requires requires (T& value, U&& by) { value -= std::forward<U>(by); }
		Serial& operator-=(U&& by) { m_value -= std::forward<U>(by); Changed(); return *this; };

		template <typename U> requires requires (T& value, U&& by) { value *= std::forward<U>(by); }
		Serial& operator*=(U&& by) { m_value *= std::forward<U>(by); Changed(); return *this; };

		template <typename U> requires requires (T& value, U&& by) { value /= std::forward<U>(by); }
		Serial& operator/=(U&& by) { m_value /= std::forward<U>(by); Changed(); return *this; };

		Serial& operator++() requires requires (T& value) { ++value; } { ++m_value; Changed(); return *this; };
		Serial& operator--() requires requires (T& value) { --value; } { --m_value; Changed(); return *this; };
		T operator++(int) requires requires (T& value) { value++; } { T before = m_value++; Changed(); return before; };
		T operator--(int) requires requires (T& value) { value--; } { T before = m_value--; Changed(); return before; };

		#pragma endregion

		#pragma region Access

		// Read like the plain member it replaces, without counting as a change
		// however it is held. Changing it in place takes *, -> or one of the
		// operators above, each of which counts.
		operator const T& () const { return m_value; };

		T& operator*() requires (!std::is_pointer_v<T>) { Changed(); return m_value; };
		const T& operator*() const { return m_value; };

		// A Serial holding a pointer hands the pointer over, so a reference
		// field reaches what it points at rather than the field itself.
		auto operator->()
		{
			if constexpr (std::is_pointer_v<T>)
				return m_value;
			else
			{
				Changed();
				return &m_value;
			};
		};

		auto operator->() const
		{
			if constexpr (std::is_pointer_v<T>)
				return m_value;
			else return &m_value;
		};

		// Goes up each time the value is changed: assigned, reached into with
		// * or -> without const, or written through its SerializedField. While
		// it holds still, so does the value, so whatever is made from it, such
		// as a mesh's vertices on the GPU, need not be made again. It starts
		// where no other field's has, so a field made where another one was
		// never passes for it. Reach in through a const Serial to look without
		// counting.
		unsigned long long Version() const { return m_version; };

		#pragma endregion

	private:
		#pragma region Registration

		void Changed()
		{
			m_version++;
			SerialChanged(m_owner, std::is_pointer_v<T>);
		};

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
				m_owner = RegisterSerialField(
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
					},
					name.value,
					&m_version);
			}
			else if constexpr (FieldTypeOf<T>() == FieldType::Asset)
				m_owner = RegisterSerialField(FieldType::Asset, Data(), nullptr, nullptr, nullptr, name.value, &m_version, T::fileExtensions);
			else m_owner = RegisterSerialField(FieldTypeOf<T>(), Data(), nullptr, nullptr, nullptr, name.value, &m_version);
		};

		// The vector types are written out of their components, so the field
		// points at those rather than at the struct around them, and an Asset
		// or MultilineText out of the string it is.
		void* Data()
		{
			constexpr FieldType type = FieldTypeOf<T>();

			if constexpr (
				type == FieldType::Vec2 ||
				type == FieldType::Vec3 ||
				type == FieldType::Vec4)
				return glm::value_ptr(m_value);
			else if constexpr (type == FieldType::Asset || type == FieldType::MultilineText)
				return static_cast<std::string*>(&m_value);
			else return &m_value;
		};

		#pragma endregion

		#pragma region State

		T m_value;
		unsigned long long m_version = 0;
		LoomObject* m_owner = nullptr;

		#pragma endregion
	};
};
