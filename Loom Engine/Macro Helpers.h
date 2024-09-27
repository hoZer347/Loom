#pragma once

#pragma warning(disable : 5103)

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
#define HAS_FUNCTION_TEST(class_name, function_name) has_##function_name##<##class_name##>::value
//
////////////////////////////////////////////////////////////////////////////////////////////////////


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


// Registers a variable to be serialized
#define REGISTER(variable_name)\


// Declares a variable to be serialized
// TODO: Implement this
#define SERIALIZED(variable_type, variable_name, default_value)\
variable_type variable_name## =\
	std::function<##variable_type##()>([]()\
	{\
\
		return default_value##;\
	})();\

#define SERIALIZED_VARIABLE(type, variable_name, default_value) SERIALIZED(type, variable_name, default_value);
