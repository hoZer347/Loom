#pragma once

#include "Loom API.h"

#include "Guid.h"

#include <string>


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
	};

	/**
	* Loom::SerializedField
	* - One member of a LoomObject that the scene format knows about
	* - Holds what type it is and where it lives; the pointer is into the object
	*   that registered the field, so a field never outlives its owner
	* - It has no name of its own: a field is identified by where it sits in the
	*   list, which is the order the Serial members were declared in. The
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

		// The scene-file text for the current value, and the reverse. Reading a
		// reference resolves the guid against the objects that exist right now
		// and answers false when the target has not been built yet, which is the
		// loader's cue to come back to it.
		std::string Write() const;
		bool Read(const std::string& text) const;

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
