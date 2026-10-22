#pragma once

#include "Loom API.h"

#include "Guid.h"
#include "Serial.h"
#include "SerializedField.h"

#include <atomic>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <mutex>
#include <utility>
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

		// A Button this object declared: its name, as a Serial's, what pressing
		// it runs, and how many fields were declared before it.
		struct FieldButton
		{
			std::string name;
			const std::function<void()>* press = nullptr;
			size_t fieldsBefore = 0;
		};

		// The Buttons this object declared, in the order they were declared.
		const std::vector<FieldButton>& GetButtons() const { return m_buttons; };

		// Whether a field is written when the object is: to a scene, a prefab,
		// the undo history, or anything else made from that text. One that is
		// not is left as it is when the object is read back, for whatever made
		// it to make again.
		virtual bool IsSaved(const SerializedField& field) const { return true; };

		// For an integer field that indexes a list only the object knows, such
		// as an Animator's clips, the names the editor offers in its place, as
		// it does an enum's. Empty for a field that is just a number.
		virtual std::vector<std::string> GetFieldChoices(const SerializedField& field) { return { }; };

		// What may have changed of what the scene format writes, since it was
		// last taken: the objects whose name, guid or fields were written, or
		// for a GameObject its components, the GameObjects whose parent or
		// next sibling may be another, and the objects destroyed. Kept only
		// while watched, so whoever looks at it looks at what changed rather
		// than at every object.
		struct Journal
		{
			struct Touched
			{
				LoomObject* object = nullptr;
				bool own = false;
				bool placed = false;
			};

			std::vector<Touched> touched{ };

			// Gone by now: only for finding what was kept under the address.
			std::vector<const LoomObject*> destroyed{ };
		};

		// Puts this object in the journal, while it is watched: for what it
		// holds, or with placed for where it sits.
		void Touch(bool placed = false);

		// Starting to watch again counts up WatchCount, since whatever was
		// kept by address while nothing was watched may since be gone. Stopping
		// throws away the journal.
		static void Watch(bool watch);
		static unsigned long long WatchCount() { return watch_count.load(); };
		static Journal TakeJournal();

	protected:
		std::string m_name;
		static inline std::recursive_mutex mutex;
		static inline std::unordered_map<uint64_t, LoomObject*> by_id{ };
		static inline std::unordered_map<std::string, LoomObject*> by_name{ };
		static inline std::unordered_map<Guid, LoomObject*> by_guid{ };

		uint64_t Initialize();

		// Declaring a Serial member is what fills these in.
		friend LOOM_API LoomObject* RegisterSerialField(
			FieldType type,
			void* data,
			LoomObject* (*get_reference)(void*),
			void (*set_reference)(void*, LoomObject*),
			bool (*accepts_reference)(LoomObject*),
			const char* name,
			unsigned long long* version);

		std::vector<SerializedField> m_fields{ };

		// Declaring a Button member is what fills this in.
		friend LOOM_API void RegisterButton(const char* name, const std::function<void()>* press);

		std::vector<FieldButton> m_buttons{ };

	private:
		friend LOOM_API void SerialChanged(LoomObject* owner, bool reference);

		static inline std::atomic<bool> watching = false;
		static inline std::atomic<unsigned long long> watch_count = 0;
		static inline std::mutex journal_mutex{ };
		static inline std::vector<LoomObject*> journal{ };
		static inline std::vector<const LoomObject*> destroyed{ };

		// Which of the journal's kinds of change this object is in it for, and
		// where, so a destructor takes it out without looking.
		static constexpr unsigned char touched_self = 1;
		static constexpr unsigned char touched_placed = 2;
		std::atomic<unsigned char> m_touched = 0;
		size_t m_journalAt = 0;

		// Who points at whom, so a dying object scrubs only the fields that
		// point at it. A holder whose references were written since is dirty
		// and indexed again before the index is next used.
		static inline std::unordered_map<const LoomObject*, std::unordered_set<LoomObject*>> referrers{ };
		static inline std::vector<LoomObject*> dirty_holders{ };
		std::vector<LoomObject*> m_referenced{ };
		std::atomic<bool> m_referencesDirty = false;
		size_t m_dirtyAt = 0;

		void ReferencesWritten();
		static void Reindex();

		// A name as a Serial or Button was declared with, a ".../" in front put
		// back to the category of the one declared before it.
		std::string TakeName(const char* given) const;

		static LoomObject* _GetByID(const uint64_t& ID);
		static LoomObject* _GetByName(const std::string& name);
		static LoomObject* _GetByGuid(const Guid& guid);
	};
};
