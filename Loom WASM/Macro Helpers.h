#pragma once

#pragma warning(disable : 5103)

#include <algorithm>


////////////////////////////////////////////////////////////////////////////////////////////////////
//
//	HAS_FUNCTION_DECL: Sets up a template function that returns true if T contains "function_name"
//	HAS_FUNCTION_TEST: The test result set up by HAS_FUNCTION_IMPL
//	- Note:
//		- HAS_FUNCTION_DECL must be called globally
//		- HAS_FUNCTION_TEST must be called AFTER HAS_FUNCTION_IMPL
//	- Example:
//	HAS_FUNCTION_DECL(push_back)
//	HAS_FUNCTION_TEST(std::vector<int>, push_back) -> TRUE
//
#define HAS_FUNCTION_DECL(function_name)\
	template <typename T>\
	class has_##function_name## {\
	private:\
		template <typename U>\
		static constexpr auto check(int) -> decltype(std::declval<U>().##function_name##(), std::true_type{})\
		{\
			return std::true_type{};\
		};\
\
		template <typename>\
		static constexpr std::false_type check(...)\
		{\
			return std::false_type{};\
		};\
\
	public:\
		static constexpr bool value = decltype(check<T>(0))::value;\
	};
#define HAS_FUNCTION_TEST(class_name, function_name) has_##function_name<class_name>::value
//
////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////
//
//	HAS_VARIABLE_DECL: Sets up a template function that returns true if T contains "member_variable_name"
//	HAS_VARIABLE_TEST: The test result set up by HAS_VARIABLE_IMPL
//	- Note:
//		- HAS_VARIABLE_DECL must be called globally
//		- HAS_VARIABLE_TEST must be called AFTER HAS_VARIABLE_IMPL
//	- Example:
//	HAS_VARIABLE_DECL(size)
//	HAS_VARIABLE_TEST(std::vector<int>, size) -> TRUE
//
#define HAS_VARIABLE_DECL(member_variable_name) \
	template <typename T> \
	class has_##member_variable_name## { \
	private: \
		template <typename U> \
		static constexpr auto check(int) -> decltype(std::declval<U>().member_variable_name, std::true_type{}) \
		{ \
			return std::true_type{}; \
		} \
		\
		template <typename> \
		static constexpr std::false_type check(...) \
		{ \
			return std::false_type{}; \
		} \
		\
	public: \
		static constexpr bool value = decltype(check<T>(0))::value; \
	};
#define HAS_VARIABLE_TEST(class_name, member_variable_name) has_##member_variable_name##<class_name>::value
//
////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: Likely not the optimal way to implement serialization
#define SERIALIZE(...)\
void OnSerialize() override\
{\
	std::cout << "Serializing: " << typeid(*this).name() << std::endl;\
	Serialize(GetName().c_str(), this, __VA_ARGS__);\
};\
void OnDeserialize() override\
{\
	std::cout << "Deserializing: " << typeid(*this).name() << std::endl;\
	Deserialize(GetName().c_str(), this, __VA_ARGS__);\
};\

////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Str: Used to put unsigned char*'s as template arguments
// - Example:
//		template <Str str> void Print<Str str>()
//		{
//			std::cout << str << std::endl;
//		};
// 
//		Print<"Hello World">(); => Hello World!
//
template<size_t N>
struct Str final
{
	constexpr Str(const char(&str)[N])
	{
		std::copy_n(str, N, value);
	};
	char value[N];
};
//
////////////////////////////////////////////////////////////////////////////////////////////////////


#define VARIABLE_NAME(name) #name
