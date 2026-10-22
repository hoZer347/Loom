#pragma once

#include "Loom API.h"

#include "Guid.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>


namespace Loom
{
	struct LoomObject;

	enum struct FieldType
	{
		Bool,
		Int,
		UInt,
		Int64,
		UInt64,
		Float,
		Double,
		String,
		Vec2,
		Vec3,
		Vec4,
		FloatArray,

		// A pointer to another LoomObject. Written as that object's guid, which
		// is the whole reason objects carry one.
		Reference,

		// Values for a shader's variables. The inspector leaves them to the
		// component, which knows the shader that declares them.
		Uniforms,

		// A state machine's state, written as the name it is registered under.
		State,
	};

	// In front of a Serial's or Button's name, the category of the one
	// declared before it.
	inline constexpr std::string_view previous_category = ".../";

	// The part of a Serial's or Button's name after its categories, which the
	// editor labels it with, and the categories before it, which it groups it
	// under. Either is empty when the name has none.
	LOOM_API std::string_view NameDisplay(std::string_view name);
	LOOM_API std::string_view NameCategory(std::string_view name);

	/**
	* Loom::SerializedField
	* - One member of a LoomObject that the scene format knows about
	* - Holds what type it is and where it lives; the pointer is into the object
	*   that registered the field, so a field never outlives its owner
	* - A field is identified by where it sits in the list, which is the order
	*   the Serial members were declared in. Its name, when its Serial was given
	*   one, is only what the editor labels and groups it by; otherwise the
	*   editor labels it after the member that holds it
	*/
	struct LOOM_API SerializedField final
	{
		FieldType type = FieldType::Int;
		void* data = nullptr;

		// Reference fields only. A member declared as some derived type cannot
		// be assigned through a LoomObject** - its LoomObject subobject is not
		// necessarily at offset zero - so the casts are generated where the real
		// type is still known.
		LoomObject* (*get_reference)(void*) = nullptr;
		void (*set_reference)(void*, LoomObject*) = nullptr;
		bool (*accepts_reference)(LoomObject*) = nullptr;

		// The Serial's count of changes, which Read and SetReference add to.
		// Whatever else writes through data calls Changed after.
		unsigned long long* version = nullptr;
		void Changed() const;

		// The object that declared the field.
		LoomObject* owner = nullptr;

		// The name the Serial was given, if any: "Display", or "Category/Display"
		// with '/' between nested categories, a ".../" in front already put
		// back to the category it stood for. Copied, so it outlives the module
		// that declared it.
		std::string name;

		// The part of the name after its categories, which the editor labels
		// the field with, and the categories before it, which it groups the
		// field under. Either is empty when the name has none.
		std::string_view Display() const;
		std::string_view Category() const;

		// The scene-file text for the current value, and the reverse. Reading a
		// reference resolves the guid against the objects that exist right now
		// and answers false when the target has not been built yet, which is the
		// loader's cue to come back to it.
		std::string Write() const;
		bool Read(const std::string& text) const;

	private:
		bool ReadValue(const std::string& text) const;

		// A long FloatArray's last Write, kept with the values it was written
		// from: writing a mesh's vertices out again each time the editor
		// compares a scene is most of what that costs. The copies of a field
		// share it, as they share the values.
		struct WrittenArray
		{
			std::vector<float> values;
			std::string text;
		};

		mutable std::shared_ptr<WrittenArray> m_written{ };

	public:

		LoomObject* GetReference() const;
		void SetReference(LoomObject* object) const;

		// What handing this field an object would put in it: the object itself
		// when the field holds its type, otherwise the first of a GameObject's
		// components that fits, so a GameObject fills a field declared as one of
		// its components. A scene stands in for its root GameObject, which the
		// hierarchy draws as the scene. Null when nothing fits.
		LoomObject* ReferenceFor(LoomObject* object) const;
	};

	// A member's identifier as a label: "m_fieldOfView" reads "Field Of View".
	LOOM_API std::string NameFromIdentifier(const char* identifier);
};
