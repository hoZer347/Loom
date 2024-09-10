export module LoomObject;

import Engine;

import <unordered_map>;
import <string>;
import <mutex>;


namespace Loom
{
	typedef unsigned long long uint64_t;

	export struct LoomObject
	{
		const uint64_t m_ID = Initialize();

		const std::string& GetName();
		void SetName(const std::string& name);

		template <typename T>
		constexpr static const T* GetByID(const uint64_t& ID)
		{
			return static_cast<T*>(_GetByID(ID));
		};

		template <typename T>
		constexpr static T* GetByName(const std::string& name)
		{
			return static_cast<T*>(_GetByName(name));
		};

		virtual ~LoomObject();

	protected:
		std::string m_name;
		static inline std::recursive_mutex mutex;
		static inline std::unordered_map<uint64_t, LoomObject*> by_id{ };
		static inline std::unordered_map<std::string, LoomObject*> by_name{ };

		uint64_t Initialize();

		constexpr std::string NameAndID() const;

	private:
		static LoomObject* _GetByID(const uint64_t& ID);
		static LoomObject* _GetByName(const std::string& name);
	};
};
