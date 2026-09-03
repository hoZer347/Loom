#pragma once

#include <cmath>

namespace Loom
{
namespace Math
{
	template <typename T = float>
	struct alignas(sizeof(T) * 2) vec2
	{
		T data[2];
	};

	template <typename T = float>
	struct vec3
	{
		vec3() : data{ T(0), T(0), T(0) } { };

		vec3(const T& x, const T& y, const T& z)
		{
			data[0] = x;
			data[1] = y;
			data[2] = z;
		};

		T data[3];

		// Named access, so the vector reads the way the code using it talks about it.
		T& x() { return data[0]; };
		T& y() { return data[1]; };
		T& z() { return data[2]; };

		const T& x() const { return data[0]; };
		const T& y() const { return data[1]; };
		const T& z() const { return data[2]; };

		vec3 operator+(const vec3& rhs) const
		{
			return vec3(data[0] + rhs.data[0], data[1] + rhs.data[1], data[2] + rhs.data[2]);
		};

		vec3 operator-(const vec3& rhs) const
		{
			return vec3(data[0] - rhs.data[0], data[1] - rhs.data[1], data[2] - rhs.data[2]);
		};

		vec3 operator*(const T& scalar) const
		{
			return vec3(data[0] * scalar, data[1] * scalar, data[2] * scalar);
		};

		vec3 operator/(const T& scalar) const
		{
			return vec3(data[0] / scalar, data[1] / scalar, data[2] / scalar);
		};

		vec3& operator+=(const vec3& rhs)
		{
			data[0] += rhs.data[0];
			data[1] += rhs.data[1];
			data[2] += rhs.data[2];

			return *this;
		};

		vec3& operator-=(const vec3& rhs)
		{
			data[0] -= rhs.data[0];
			data[1] -= rhs.data[1];
			data[2] -= rhs.data[2];

			return *this;
		};

		vec3& operator*=(const T& scalar)
		{
			data[0] *= scalar;
			data[1] *= scalar;
			data[2] *= scalar;

			return *this;
		};

		bool operator==(const vec3& rhs) const
		{
			return data[0] == rhs.data[0]
				&& data[1] == rhs.data[1]
				&& data[2] == rhs.data[2];
		};

		T Magnitude() const
		{
			return T(std::sqrt(double(SqrMagnitude())));
		};

		T SqrMagnitude() const
		{
			return data[0] * data[0] + data[1] * data[1] + data[2] * data[2];
		};

		// Zero-safe, so normalising a stopped velocity is not a divide by zero.
		vec3 Normalized() const
		{
			const T length = Magnitude();

			return length > T(0) ? *this / length : vec3();
		};

		static vec3 Zero()
		{
			return vec3();
		};

		/// Shortens the vector to at most max_length, leaving a shorter one alone --
		/// Unity's Vector3.ClampMagnitude, which is what an analogue stick's read wants.
		static vec3 ClampMagnitude(const vec3& value, const T& max_length)
		{
			const T sqr = value.SqrMagnitude();

			if (sqr <= max_length * max_length)
				return value;

			return value.Normalized() * max_length;
		};

		/// Steps from current towards target by at most max_delta, and never overshoots --
		/// Unity's Vector3.MoveTowards. The difference from a lerp is that the step is an
		/// absolute distance, so an acceleration stays an acceleration at any framerate.
		static vec3 MoveTowards(const vec3& current, const vec3& target, const T& max_delta)
		{
			const vec3 difference = target - current;
			const T distance = difference.Magnitude();

			if (distance <= max_delta || distance == T(0))
				return target;

			return current + difference / distance * max_delta;
		};
	};

	template <typename T = float>
	struct alignas(sizeof(T) * 4) vec4
	{
		T data[4];
	};

	template <typename T = float>
	struct alignas(sizeof(T) * 16) mat4
	{
		T data[16];
	};
};
};
