#pragma once

#include "Loom API.h"

#include "Guid.h"
#include "Serial.h"
#include "SerializedField.h"

#include <unordered_map>
#include <string>
#include <mutex>
#include <vector>


namespace Loom
{
	typedef unsigned long long uint64_t;

	struct LOOM_API LoomObject
	{
	private:
		// Declared first so it exists by the time Initialize() runs and puts the
		// object in the lookup tables.
		Guid m_guid = Guid::New();

	public:
		const uint64_t m_ID = Initialize();

		// Objects are identities, not values: every field registered on one
		// points into that exact instance.
		LoomObject(const LoomObject&) = delete;
		LoomObject& operator=(const LoomObject&) = delete;

		LoomObject() = default;

		const std::string& GetName();
		void SetName(const std::string& name);

		// The identity that goes into scene files. SetGuid exists for the
		// loader, which has to put an object back under the guid it was saved
		// with so references to it still land.
		const Guid& GetGuid() const { return m_guid; };
		void SetGuid(const Guid& guid);

		template <typename T = LoomObject>
		constexpr static const T* GetByID(const uint64_t& ID)
		{
			return static_cast<T*>(_GetByID(ID));
		};

		template <typename T = LoomObject>
		constexpr static T* GetByName(const std::string& name)
		{
			return static_cast<T*>(_GetByName(name));
		};

		template <typename T = LoomObject>
		static T* GetByGuid(const Guid& guid)
		{
			return dynamic_cast<T*>(_GetByGuid(guid));
		};

		virtual ~LoomObject();

		std::string NameAndID() const;

		// The Serial members this object declared, in the order they were
		// declared, which is also how they are identified: the scene format
		// writes them by position, and the editor's inspector draws them.
		const std::vector<SerializedField>& GetFields() const { return m_fields; };

		// After the editor writes one of those fields, for an object whose other
		// state has to follow it.
		virtual void OnFieldChanged(const SerializedField& field) { };

	protected:
		std::string m_name;
		static inline std::recursive_mutex mutex;
		static inline std::unordered_map<uint64_t, LoomObject*> by_id{ };
		static inline std::unordered_map<std::string, LoomObject*> by_name{ };
		static inline std::unordered_map<Guid, LoomObject*> by_guid{ };

		// Only the objects that hold a Reference field, so a dying object has a
		// short list to scrub rather than every object alive.
		static inline std::vector<LoomObject*> referencing{ };

		uint64_t Initialize();

		// Declaring a Serial member is what fills these in.
		friend LOOM_API void RegisterSerialField(
			FieldType type,
			const char* name,
			void* data,
			LoomObject* (*get_reference)(void*),
			void (*set_reference)(void*, LoomObject*));

		std::vector<SerializedField> m_fields{ };

		// Whether any of them is a Reference, which is what puts this object in
		// the referencing list and what a destructor has to undo.
		bool m_references = false;

	private:
		static LoomObject* _GetByID(const uint64_t& ID);
		static LoomObject* _GetByName(const std::string& name);
		static LoomObject* _GetByGuid(const Guid& guid);
	};
};
