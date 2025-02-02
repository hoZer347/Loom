#pragma once

#include <unordered_map>
#include <string>
#include <mutex>


namespace Loom
{
	typedef unsigned long long uint64_t;

	struct LoomObject
	{
		const uint64_t m_ID = Initialize();

		const std::string& GetName();
		void SetName(const std::string& name);

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

		virtual ~LoomObject();

		constexpr std::string NameAndID() const;

	protected:
		std::string m_name;
		static inline std::recursive_mutex mutex;
		static inline std::unordered_map<uint64_t, LoomObject*> by_id{ };
		static inline std::unordered_map<std::string, LoomObject*> by_name{ };

		uint64_t Initialize();

		// Use the SERIALIZE(...) macro to serialize members
		virtual void OnSerialize() {};
		virtual void OnDeserialize() {};
		//

	private:
		static LoomObject* _GetByID(const uint64_t& ID);
		static LoomObject* _GetByName(const std::string& name);
	};
};
