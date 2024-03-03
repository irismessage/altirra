//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2023 Avery Lee, All Rights Reserved.
//
//	Beginning with 1.6.0, the VirtualDub system library is licensed
//	differently than the remainder of VirtualDub.  This particular file is
//	thus licensed as follows (the "zlib" license):
//
//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.

#ifndef f_VD2_SYSTEM_CONSTEXPR_H
#define f_VD2_SYSTEM_CONSTEXPR_H

#include <bit>
#include <iterator>
#include <type_traits>
#include <math.h>

template<typename T, T N> struct VDCxPrint;

template<typename T, size_t N>
struct VDCxArray {
	using value_type      = T;
	using size_type       = size_t;
	using difference_type = ptrdiff_t;
	using reference       = T&;
	using const_reference = const T&;
	using pointer         = T*;
	using const_pointer   = const T*;
	using iterator        = T*;
	using const_iterator  = const T*;
	using reverse_iterator       = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

	T v[N];

	[[nodiscard]] constexpr T&       operator[](size_t i)       { return v[i]; }
	[[nodiscard]] constexpr const T& operator[](size_t i) const { return v[i]; }

	[[nodiscard]] constexpr       T *begin()       { return v; }
	[[nodiscard]] constexpr const T *begin() const { return v; }
	[[nodiscard]] constexpr       T *end()       { return v+N; }
	[[nodiscard]] constexpr const T *end() const { return v+N; }

	[[nodiscard]] constexpr       T *data()       { return v; }
	[[nodiscard]] constexpr const T *data() const { return v; }

	[[nodiscard]] constexpr       T& front()       { return v[0]; }
	[[nodiscard]] constexpr const T& front() const { return v[0]; }

	[[nodiscard]] constexpr       T& back()       { return v[N-1]; }
	[[nodiscard]] constexpr const T& back() const { return v[N-1]; }

	[[nodiscard]] constexpr bool   empty() const { return false; }
	[[nodiscard]] constexpr size_t size() const { return N; }

	template<typename T_Src>
	static constexpr VDCxArray transform(T_Src&& input, auto&& fn) {
		VDCxArray output {};

		for(size_t i=0; i<N; ++i)
			output.v[i] = fn(input[i]);

		return output;
	}

	static constexpr VDCxArray from_index(auto&& fn) {
		VDCxArray output {};

		for(size_t i=0; i<N; ++i)
			output.v[i] = fn(T(i));

		return output;
	}
};

inline constexpr float VDCxNarrowToFloat(double d) {
	// #pragma float_control isn't enough to force correct narrowing with
	// fast math, so use the bit_cast sledgehammer to prevent the compiler from
	// cheating.
	return std::bit_cast<float>(std::bit_cast<uint32>((float)d));
}

// Compute sin(pi*x).
constexpr double VDCxSinPiD(double x) {
	// mirror across v=0 antisymmetry
	float sign = 1;

	if (x < 0) {
		x = -x;
		sign = -1;
	}

	// guard against integers exceeding a long long -- these will be even integers
	// and thus guaranteed to result in 0
	if (x >= 18014398509481984.0)
		return 0;

	// wrap around 360
	x -= (long long)(x * 0.5) * 2;

	// mirror across v=1 antisymmetry
	if (x > 1.0) {
		sign = -sign;
		x -= 1.0;
	}

	// mirror across v=0.5 symmetry
	if (x > 0.5)
		x = 1.0 - x;

	// compute series expansion over first quadrant
	// need to split into octants to avoid cancellation related issues
	if (x > 0.25) {
		x = 0.5 - x;
		const double x2 = x*x;
		return sign*(
			((((((((-0.00000013879*x2 + 0.00000430307)*x2 - 0.000104638105)*x2 + 0.001929574309)*x2 - 0.02580689139)*x2 + 0.235330630359)*x2 - 1.335262768855)*x2 + 4.058712126417)*x2 - 4.934802200545)*x2 + 1.0
		);
	} else {
		const double x2 = x*x;

		return sign*(
			((((((((0.000000795205*x2 - 0.000021915353)*x2 + 0.000466302806)*x2 - 0.007370430946)*x2 + 0.082145886611)*x2 - 0.599264529321)*x2 + 2.550164039877)*x2 - 5.16771278005)*x2 + 3.14159265359)*x
		);
	}
}

constexpr float VDCxSinPi(float x) {
	if (std::is_constant_evaluated()) {
		return VDCxNarrowToFloat(VDCxSinPiD(x));
	} else {
		return VDCxNarrowToFloat(sin((double)x * 3.1415926535897932384626433832795));
	}
}

// Compute cos(pi*x).
constexpr double VDCxCosPiD(double x) {
	// mirror across v=0 symmetry
	if (x < 0)
		x = -x;

	// guard against integers exceeding a long long -- these will be even integers
	// and thus guaranteed to result in 1
	if (x >= 18014398509481984.0)
		return 1;

	// wrap around 360
	x -= (long long)(x * 0.5) * 2;

	// mirror across v=1 symmetry
	if (x > 1.0)
		x = 2.0 - x;

	// mirror across v=0.5 antisymmetry
	float sign = 1;

	if (x > 0.5) {
		sign = -1;
		x = 1.0 - x;
	}

	// compute series expansion over first quadrant
	// need to split into octants to avoid cancellation related issues
	if (x > 0.25) {
		x = 0.5 - x;
		const double x2 = x*x;

		return sign*(
			((((((((0.000000795205*x2 - 0.000021915353)*x2 + 0.000466302806)*x2 - 0.007370430946)*x2 + 0.082145886611)*x2 - 0.599264529321)*x2 + 2.550164039877)*x2 - 5.16771278005)*x2 + 3.14159265359)*x
		);
	} else {
		const double x2 = x*x;
		return sign*(
			((((((((-0.00000013879*x2 + 0.00000430307)*x2 - 0.000104638105)*x2 + 0.001929574309)*x2 - 0.02580689139)*x2 + 0.235330630359)*x2 - 1.335262768855)*x2 + 4.058712126417)*x2 - 4.934802200545)*x2 + 1.0
		);
	}
}

constexpr float VDCxCosPi(float x) {
	if (std::is_constant_evaluated()) {
		return VDCxNarrowToFloat(VDCxCosPiD(x));
	} else {
		return VDCxNarrowToFloat(cos((double)x * 3.1415926535897932384626433832795));
	}
}

constexpr float VDCxSin(float v) {
	if (std::is_constant_evaluated())
		return VDCxNarrowToFloat(VDCxSinPiD(v * 0.31830988618379067153776752674503));
	else
		return sinf(v);
}

constexpr float VDCxCos(float v) {
	if (std::is_constant_evaluated())
		return VDCxNarrowToFloat(VDCxSinPiD(v * 0.31830988618379067153776752674503));
	else
		return cosf(v);
}

// Computes sinc(pi*x) = sin(pi*x)/(pi*x) with limit handling for x=0.
constexpr float VDCxSincPi(float v) {
	if (v >= -1e-8f && v <= 1e-8f)
		return 1.0f;

	if (std::is_constant_evaluated())
		return VDCxNarrowToFloat(VDCxSinPiD(v) / (3.1415926535897932384626433832795 * v));
	else {
		const double d = (double)v * 3.1415926535897932384626433832795;
		return VDCxNarrowToFloat(sin(d) / d);
	}
}

#endif
