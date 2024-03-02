//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2019 Avery Lee, All Rights Reserved.
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

#ifndef f_VD2_SYSTEM_VECMATH_SSE2_H
#define f_VD2_SYSTEM_VECMATH_SSE2_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/vectors.h>
#include <vd2/system/win32/intrin.h>

namespace nsVDVecMath {
	struct vdmask32x3 {
		__m128 v;
	};

	struct vdmask32x4 {
		__m128 v;
	};

	struct vdfloat32x3 {
		__m128 v;

		static vdfloat32x3 zero() {
			return vdfloat32x3 { _mm_setzero_ps() };
		}

		static vdfloat32x3 set1(float x) {
			return vdfloat32x3 { _mm_set1_ps(x) };
		}

		static vdfloat32x3 set(float x, float y, float z) {
			return vdfloat32x3 { _mm_set_ps(z, z, y, x) };
		}

		float x() const { return _mm_cvtss_f32(v); }
		float y() const { return _mm_cvtss_f32(_mm_shuffle_ps(v, v, 0x55)); }
		float z() const { return _mm_cvtss_f32(_mm_shuffle_ps(v, v, 0xAA)); }
	};

	inline vdfloat32x3 operator+(vdfloat32x3 x) { return x; }
	inline vdfloat32x3 operator-(vdfloat32x3 x) { return vdfloat32x3 { _mm_sub_ps(_mm_setzero_ps(), x.v) }; }

	inline vdfloat32x3 operator+(vdfloat32x3 x, vdfloat32x3 y) { return vdfloat32x3 { _mm_add_ps(x.v, y.v) }; }
	inline vdfloat32x3 operator-(vdfloat32x3 x, vdfloat32x3 y) { return vdfloat32x3 { _mm_sub_ps(x.v, y.v) }; }
	inline vdfloat32x3 operator*(vdfloat32x3 x, vdfloat32x3 y) { return vdfloat32x3 { _mm_mul_ps(x.v, y.v) }; }
	inline vdfloat32x3 operator/(vdfloat32x3 x, vdfloat32x3 y) { return vdfloat32x3 { _mm_div_ps(x.v, y.v) }; }

	inline vdfloat32x3 operator+(vdfloat32x3 x, float y) { return vdfloat32x3 { _mm_add_ps(x.v, _mm_set1_ps(y)) }; }
	inline vdfloat32x3 operator-(vdfloat32x3 x, float y) { return vdfloat32x3 { _mm_sub_ps(x.v, _mm_set1_ps(y)) }; }
	inline vdfloat32x3 operator*(vdfloat32x3 x, float y) { return vdfloat32x3 { _mm_mul_ps(x.v, _mm_set1_ps(y)) }; }
	inline vdfloat32x3 operator/(vdfloat32x3 x, float y) { return vdfloat32x3 { _mm_div_ps(x.v, _mm_set1_ps(y)) }; }

	inline vdfloat32x3 operator+(float x, vdfloat32x3 y) { return vdfloat32x3 { _mm_add_ps(_mm_set1_ps(x), y.v) }; }
	inline vdfloat32x3 operator-(float x, vdfloat32x3 y) { return vdfloat32x3 { _mm_sub_ps(_mm_set1_ps(x), y.v) }; }
	inline vdfloat32x3 operator*(float x, vdfloat32x3 y) { return vdfloat32x3 { _mm_mul_ps(_mm_set1_ps(x), y.v) }; }
	inline vdfloat32x3 operator/(float x, vdfloat32x3 y) { return vdfloat32x3 { _mm_div_ps(_mm_set1_ps(x), y.v) }; }

	inline vdfloat32x3& operator+=(vdfloat32x3& x, vdfloat32x3 y) { x.v = _mm_add_ps(x.v, y.v); return x; }
	inline vdfloat32x3& operator-=(vdfloat32x3& x, vdfloat32x3 y) { x.v = _mm_sub_ps(x.v, y.v); return x; }
	inline vdfloat32x3& operator*=(vdfloat32x3& x, vdfloat32x3 y) { x.v = _mm_mul_ps(x.v, y.v); return x; }
	inline vdfloat32x3& operator/=(vdfloat32x3& x, vdfloat32x3 y) { x.v = _mm_div_ps(x.v, y.v); return x; }

	inline vdfloat32x3& operator+=(vdfloat32x3& x, float y) { x.v = _mm_add_ps(x.v, _mm_set1_ps(y)); return x; }
	inline vdfloat32x3& operator-=(vdfloat32x3& x, float y) { x.v = _mm_sub_ps(x.v, _mm_set1_ps(y)); return x; }
	inline vdfloat32x3& operator*=(vdfloat32x3& x, float y) { x.v = _mm_mul_ps(x.v, _mm_set1_ps(y)); return x; }
	inline vdfloat32x3& operator/=(vdfloat32x3& x, float y) { x.v = _mm_div_ps(x.v, _mm_set1_ps(y)); return x; }

	inline vdmask32x3 operator< (vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { _mm_cmplt_ps(x.v, y.v) }; }
	inline vdmask32x3 operator<=(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { _mm_cmple_ps(x.v, y.v) }; }
	inline vdmask32x3 operator> (vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { _mm_cmpgt_ps(x.v, y.v) }; }
	inline vdmask32x3 operator>=(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { _mm_cmpge_ps(x.v, y.v) }; }
	inline vdmask32x3 operator==(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { _mm_cmpeq_ps(x.v, y.v) }; }
	inline vdmask32x3 operator!=(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { _mm_cmpneq_ps(x.v, y.v) }; }

	struct vdfloat32x4 {
		__m128 v;

		static vdfloat32x4 zero() {
			return vdfloat32x4 { _mm_setzero_ps() };
		}

		static vdfloat32x4 set1(float x) {
			return vdfloat32x4 { _mm_set1_ps(x) };
		}

		static vdfloat32x4 set(float x, float y, float z, float w) {
			return vdfloat32x4 { _mm_set_ps(w, z, y, x) };
		}

		static vdfloat32x4 set(vdfloat32x3 xyz, float w) {
			return vdfloat32x4 { _mm_shuffle_ps(xyz.v, _mm_move_ss(xyz.v, _mm_set1_ps(w)), 0b00100100) };
		}

		static vdfloat32x4 unpacku8(uint32 v8) {
			__m128i zero = _mm_setzero_si128();

			return vdfloat32x4 {
				_mm_cvtepi32_ps(_mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)v8), zero), zero))
			};
		}

		float x() const { return _mm_cvtss_f32(v); }
		float y() const { return _mm_cvtss_f32(_mm_shuffle_ps(v, v, 0x55)); }
		float z() const { return _mm_cvtss_f32(_mm_shuffle_ps(v, v, 0xAA)); }
		float w() const { return _mm_cvtss_f32(_mm_shuffle_ps(v, v, 0xFF)); }
	};

	inline vdfloat32x4 operator+(vdfloat32x4 x) { return x; }
	inline vdfloat32x4 operator-(vdfloat32x4 x) { return vdfloat32x4 { _mm_sub_ps(_mm_setzero_ps(), x.v) }; }

	inline vdfloat32x4 operator+(vdfloat32x4 x, vdfloat32x4 y) { return vdfloat32x4 { _mm_add_ps(x.v, y.v) }; }
	inline vdfloat32x4 operator-(vdfloat32x4 x, vdfloat32x4 y) { return vdfloat32x4 { _mm_sub_ps(x.v, y.v) }; }
	inline vdfloat32x4 operator*(vdfloat32x4 x, vdfloat32x4 y) { return vdfloat32x4 { _mm_mul_ps(x.v, y.v) }; }
	inline vdfloat32x4 operator/(vdfloat32x4 x, vdfloat32x4 y) { return vdfloat32x4 { _mm_div_ps(x.v, y.v) }; }

	inline vdfloat32x4 operator+(vdfloat32x4 x, float y) { return vdfloat32x4 { _mm_add_ps(x.v, _mm_set1_ps(y)) }; }
	inline vdfloat32x4 operator-(vdfloat32x4 x, float y) { return vdfloat32x4 { _mm_sub_ps(x.v, _mm_set1_ps(y)) }; }
	inline vdfloat32x4 operator*(vdfloat32x4 x, float y) { return vdfloat32x4 { _mm_mul_ps(x.v, _mm_set1_ps(y)) }; }
	inline vdfloat32x4 operator/(vdfloat32x4 x, float y) { return vdfloat32x4 { _mm_div_ps(x.v, _mm_set1_ps(y)) }; }

	inline vdfloat32x4 operator+(float x, vdfloat32x4 y) { return vdfloat32x4 { _mm_add_ps(_mm_set1_ps(x), y.v) }; }
	inline vdfloat32x4 operator-(float x, vdfloat32x4 y) { return vdfloat32x4 { _mm_sub_ps(_mm_set1_ps(x), y.v) }; }
	inline vdfloat32x4 operator*(float x, vdfloat32x4 y) { return vdfloat32x4 { _mm_mul_ps(_mm_set1_ps(x), y.v) }; }
	inline vdfloat32x4 operator/(float x, vdfloat32x4 y) { return vdfloat32x4 { _mm_div_ps(_mm_set1_ps(x), y.v) }; }

	inline vdmask32x4 operator< (vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { _mm_cmplt_ps(x.v, y.v) }; }
	inline vdmask32x4 operator<=(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { _mm_cmple_ps(x.v, y.v) }; }
	inline vdmask32x4 operator> (vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { _mm_cmpgt_ps(x.v, y.v) }; }
	inline vdmask32x4 operator>=(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { _mm_cmpge_ps(x.v, y.v) }; }
	inline vdmask32x4 operator==(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { _mm_cmpeq_ps(x.v, y.v) }; }
	inline vdmask32x4 operator!=(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { _mm_cmpneq_ps(x.v, y.v) }; }

	///////////////////////////////////////////////////////////////////////

	struct vdfloat32x3x3 {
		vdfloat32x3 x, y, z;
	};

	inline vdfloat32x3x3 loadu(const vdfloat3x3& m) {
		__m128 v3 = _mm_loadu_ps(&m.y.z);

		return vdfloat32x3x3 {
			{ _mm_loadu_ps(&m.x.x) },
			{ _mm_loadu_ps(&m.y.x) },
			{ _mm_shuffle_ps(v3, v3, 0b11'11'10'01) }
		};
	}

	inline vdfloat32x3 mul(vdfloat32x3 a, const vdfloat32x3x3& b) {
		return vdfloat32x3 {
			_mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(a.v, a.v, 0x00), b.x.v), _mm_mul_ps(_mm_shuffle_ps(a.v, a.v, 0x55), b.y.v)), _mm_mul_ps(_mm_shuffle_ps(a.v, a.v, 0xAA), b.z.v))
		};
	}

	inline vdfloat32x3x3 mul(vdfloat32x3x3 a, vdfloat32x3x3 b) {
		return vdfloat32x3x3 {
			{ _mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(a.x.v, a.x.v, 0x00), b.x.v), _mm_mul_ps(_mm_shuffle_ps(a.x.v, a.x.v, 0x55), b.y.v)), _mm_mul_ps(_mm_shuffle_ps(a.x.v, a.x.v, 0xAA), b.z.v)) },
			{ _mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(a.y.v, a.y.v, 0x00), b.x.v), _mm_mul_ps(_mm_shuffle_ps(a.y.v, a.y.v, 0x55), b.y.v)), _mm_mul_ps(_mm_shuffle_ps(a.y.v, a.y.v, 0xAA), b.z.v)) },
			{ _mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(a.z.v, a.z.v, 0x00), b.x.v), _mm_mul_ps(_mm_shuffle_ps(a.z.v, a.z.v, 0x55), b.y.v)), _mm_mul_ps(_mm_shuffle_ps(a.z.v, a.z.v, 0xAA), b.z.v)) },
		};
	}

	///////////////////////////////////////////////////////////////////////

	struct vdfloat32x3x4 {
		vdfloat32x3 x, y, z, w;
	};

	inline vdfloat32x3 mul1(vdfloat32x3 a, const vdfloat32x3x4& b) {
		return a.x() * b.x
			+ a.y() * b.y
			+ a.z() * b.z
			+ b.w;
	}

	///////////////////////////////////////////////////////////////////////

	inline vdfloat32x3 select(vdmask32x3 mask, vdfloat32x3 x, vdfloat32x3 y) {
		return vdfloat32x3 { _mm_or_ps(_mm_and_ps(mask.v, x.v), _mm_andnot_ps(mask.v, y.v)) };
	}

	inline vdfloat32x4 select(vdmask32x4 mask, vdfloat32x4 x, vdfloat32x4 y) {
		return vdfloat32x4 { _mm_or_ps(_mm_and_ps(mask.v, x.v), _mm_andnot_ps(mask.v, y.v)) };
	}

	inline vdfloat32x4 abs(vdfloat32x4 x) {
		return vdfloat32x4 { _mm_max_ps(x.v, _mm_sub_ps(_mm_setzero_ps(), x.v)) };
	}

	inline float maxcomponent(vdfloat32x3 x) {
		return _mm_cvtss_f32(_mm_max_ps(_mm_max_ps(x.v, _mm_shuffle_ps(x.v, x.v, 0x55)), _mm_shuffle_ps(x.v, x.v, 0xAA)));
	}

	inline float maxcomponent(vdfloat32x4 x) {
		__m128 t = _mm_max_ps(x.v, _mm_shuffle_ps(x.v, x.v, 0x4E));

		return _mm_cvtss_f32(_mm_max_ps(t, _mm_shuffle_ps(t, t, 0xB1)));
	}

	inline float dot(vdfloat32x3 x, vdfloat32x3 y) {
		__m128 t = _mm_mul_ps(x.v, y.v);

		return _mm_cvtss_f32(_mm_add_ps(_mm_add_ps(t, _mm_shuffle_ps(t, t, 0x55)), _mm_shuffle_ps(t, t, 0xAA)));
	}

	inline vdfloat32x3 sqrt(vdfloat32x3 x) {
		return vdfloat32x3 { _mm_sqrt_ps(x.v) };
	}

	inline vdfloat32x4 sqrt(vdfloat32x4 x) {
		return vdfloat32x4 { _mm_sqrt_ps(x.v) };
	}

	inline vdfloat32x3 lerp(vdfloat32x3 x, vdfloat32x3 y, float f) {
		return vdfloat32x3 { _mm_add_ps(_mm_mul_ps(x.v, _mm_set1_ps(1.0f - f)), _mm_mul_ps(y.v, _mm_set1_ps(f))) };
	}

	inline vdfloat32x3 lerp(vdfloat32x3 x, vdfloat32x3 y, vdfloat32x3 f) {
		return vdfloat32x3 { _mm_add_ps(_mm_mul_ps(x.v, _mm_sub_ps(_mm_set1_ps(1.0f), f.v)), _mm_mul_ps(y.v, f.v)) };
	}

	inline vdfloat32x4 rcp(vdfloat32x4 x) {
		return vdfloat32x4 { _mm_div_ps(_mm_set1_ps(1.0f), x.v) };
	}

	inline vdfloat32x4 rcpest(vdfloat32x4 x) {
		return vdfloat32x4 { _mm_rcp_ps(x.v) };
	}

	inline vdfloat32x4 rsqrt(vdfloat32x4 x) {
		return vdfloat32x4 { _mm_div_ps(_mm_set1_ps(1.0f), _mm_sqrt_ps(x.v)) };
	}

	inline vdfloat32x4 rsqrtest(vdfloat32x4 x) {
		return vdfloat32x4 { _mm_rsqrt_ps(x.v) };
	}

	template<unsigned xsel, unsigned ysel, unsigned zsel>
	inline vdfloat32x3 permute(vdfloat32x3 x) {
		static_assert(xsel < 3 && ysel < 3 && zsel < 3);

		return vdfloat32x3 { _mm_shuffle_ps(x.v, x.v, xsel + ysel*4 + zsel*(16+64)) };
	}

	template<unsigned xsel, unsigned ysel, unsigned zsel, unsigned wsel>
	inline vdfloat32x4 permute(vdfloat32x4 x) {
		static_assert((xsel | ysel | zsel | wsel) < 4);

		return vdfloat32x4 { _mm_shuffle_ps(x.v, x.v, xsel + ysel*4 + zsel*16 + wsel*64) };
	}

	inline vdfloat32x3 min(vdfloat32x3 x, vdfloat32x3 y) { return vdfloat32x3 { _mm_min_ps(x.v, y.v) }; }
	inline vdfloat32x4 min(vdfloat32x4 x, vdfloat32x4 y) { return vdfloat32x4 { _mm_min_ps(x.v, y.v) }; }

	inline vdfloat32x3 max0(vdfloat32x3 x) { return vdfloat32x3 { _mm_max_ps(x.v, _mm_setzero_ps()) }; }
	inline vdfloat32x4 max0(vdfloat32x4 x) { return vdfloat32x4 { _mm_max_ps(x.v, _mm_setzero_ps()) }; }

	inline vdfloat32x3 max(vdfloat32x3 x, vdfloat32x3 y) { return vdfloat32x3 { _mm_max_ps(x.v, y.v) }; }
	inline vdfloat32x4 max(vdfloat32x4 x, vdfloat32x4 y) { return vdfloat32x4 { _mm_max_ps(x.v, y.v) }; }

	inline vdfloat32x3 clamp(vdfloat32x3 x, vdfloat32x3 mn, vdfloat32x3 mx) { return min(max(x, mn), mx); }
	inline vdfloat32x4 clamp(vdfloat32x4 x, vdfloat32x4 mn, vdfloat32x4 mx) { return min(max(x, mn), mx); }

	inline vdfloat32x3 saturate(vdfloat32x3 x) { return clamp(x, vdfloat32x3::set1(0), vdfloat32x3::set1(1)); }
	inline vdfloat32x4 saturate(vdfloat32x4 x) { return clamp(x, vdfloat32x4::set1(0), vdfloat32x4::set1(1)); }

	inline vdfloat32x3 pow(vdfloat32x3 x, float y) {
		return vdfloat32x3::set(
			powf(x.x(), y),
			powf(x.y(), y),
			powf(x.z(), y)
		);
	}

	inline vdfloat32x4 pow(vdfloat32x4 x, float y) {
		return vdfloat32x4::set(
			powf(x.x(), y),
			powf(x.y(), y),
			powf(x.z(), y),
			powf(x.w(), y)
		);
	}

	inline uint32 packus8(vdfloat32x3 x) {
		__m128i y = _mm_cvtps_epi32(x.v);

		y = _mm_packs_epi32(y, y);
		
		return (uint32)_mm_cvtsi128_si32(_mm_packus_epi16(y, y)) & 0xFFFFFF;
	}

	inline uint32 packus8(vdfloat32x4 x) {
		__m128i y = _mm_cvtps_epi32(x.v);

		y = _mm_packs_epi32(y, y);
		
		return (uint32)_mm_cvtsi128_si32(_mm_packus_epi16(y, y));
	}
}

using nsVDVecMath::vdfloat32x4;

#endif
