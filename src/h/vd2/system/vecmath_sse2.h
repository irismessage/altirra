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

		[[nodiscard]] static vdmask32x3 set(bool x, bool y, bool z) {
			return vdmask32x3 {
				_mm_castsi128_ps(_mm_set_epi32(
					0,
					z ? -1 : 0,
					y ? -1 : 0,
					x ? -1 : 0
				))
			};
		}
	};

	[[nodiscard]] inline vdmask32x3 operator~(vdmask32x3 a) { return vdmask32x3 { _mm_xor_ps(a.v, _mm_cmpeq_ps(_mm_setzero_ps(), _mm_setzero_ps())) }; }
	[[nodiscard]] inline vdmask32x3 operator&(vdmask32x3 a, vdmask32x3 b) { return vdmask32x3 { _mm_and_ps(a.v, b.v) }; }
	[[nodiscard]] inline vdmask32x3 operator|(vdmask32x3 a, vdmask32x3 b) { return vdmask32x3 { _mm_or_ps(a.v, b.v) }; }
	[[nodiscard]] inline vdmask32x3 operator^(vdmask32x3 a, vdmask32x3 b) { return vdmask32x3 { _mm_xor_ps(a.v, b.v) }; }

	[[nodiscard]] inline bool operator==(vdmask32x3 a, vdmask32x3 b) { return (_mm_movemask_ps(_mm_xor_ps(a.v, b.v)) & 7) == 0; }
	[[nodiscard]] inline bool operator!=(vdmask32x3 a, vdmask32x3 b) { return (_mm_movemask_ps(_mm_xor_ps(a.v, b.v)) & 7) != 0; }

	///////////////////////////////////////////////////////////////////////

	struct vdmask32x4 {
		__m128 v;

		[[nodiscard]] static vdmask32x4 set(bool x, bool y, bool z, bool w) {
			return vdmask32x4 {
				_mm_castsi128_ps(_mm_set_epi32(
					w ? -1 : 0,
					z ? -1 : 0,
					y ? -1 : 0,
					x ? -1 : 0
				))
			};
		}
	};

	[[nodiscard]] inline vdmask32x4 operator~(vdmask32x4 a) { return vdmask32x4 { _mm_xor_ps(a.v, _mm_cmpeq_ps(_mm_setzero_ps(), _mm_setzero_ps())) }; }
	[[nodiscard]] inline vdmask32x4 operator&(vdmask32x4 a, vdmask32x4 b) { return vdmask32x4 { _mm_and_ps(a.v, b.v) }; }
	[[nodiscard]] inline vdmask32x4 operator|(vdmask32x4 a, vdmask32x4 b) { return vdmask32x4 { _mm_or_ps(a.v, b.v) }; }
	[[nodiscard]] inline vdmask32x4 operator^(vdmask32x4 a, vdmask32x4 b) { return vdmask32x4 { _mm_xor_ps(a.v, b.v) }; }

	[[nodiscard]] inline bool operator!=(vdmask32x4 a, vdmask32x4 b) { return _mm_movemask_ps(_mm_xor_ps(a.v, b.v)) != 0; }
	[[nodiscard]] inline bool operator==(vdmask32x4 a, vdmask32x4 b) { return _mm_movemask_ps(_mm_xor_ps(a.v, b.v)) == 0; }

	///////////////////////////////////////////////////////////////////////

	struct vdfloat32x3 {
		__m128 v;

		[[nodiscard]] static vdfloat32x3 zero() {
			return vdfloat32x3 { _mm_setzero_ps() };
		}

		[[nodiscard]] static vdfloat32x3 set1(float x) {
			return vdfloat32x3 { _mm_set1_ps(x) };
		}

		[[nodiscard]] static vdfloat32x3 set(float x, float y, float z) {
			return vdfloat32x3 { _mm_set_ps(z, z, y, x) };
		}

		[[nodiscard]] float x() const { return _mm_cvtss_f32(v); }
		[[nodiscard]] float y() const { return _mm_cvtss_f32(_mm_shuffle_ps(v, v, 0x55)); }
		[[nodiscard]] float z() const { return _mm_cvtss_f32(_mm_shuffle_ps(v, v, 0xAA)); }

		[[nodiscard]] vdfloat32x3 operator+() { return *this; }
		[[nodiscard]] vdfloat32x3 operator-() { return vdfloat32x3 { _mm_sub_ps(_mm_setzero_ps(), v) }; }

		[[nodiscard]] vdfloat32x3 operator+(vdfloat32x3 y) const { return vdfloat32x3 { _mm_add_ps(v, y.v) }; }
		[[nodiscard]] vdfloat32x3 operator-(vdfloat32x3 y) const { return vdfloat32x3 { _mm_sub_ps(v, y.v) }; }
		[[nodiscard]] vdfloat32x3 operator*(vdfloat32x3 y) const { return vdfloat32x3 { _mm_mul_ps(v, y.v) }; }
		[[nodiscard]] vdfloat32x3 operator/(vdfloat32x3 y) const { return vdfloat32x3 { _mm_div_ps(v, y.v) }; }

		[[nodiscard]] vdfloat32x3 operator+(float y) const { return vdfloat32x3 { _mm_add_ps(v, _mm_set1_ps(y)) }; }
		[[nodiscard]] vdfloat32x3 operator-(float y) const { return vdfloat32x3 { _mm_sub_ps(v, _mm_set1_ps(y)) }; }
		[[nodiscard]] vdfloat32x3 operator*(float y) const { return vdfloat32x3 { _mm_mul_ps(v, _mm_set1_ps(y)) }; }
		[[nodiscard]] vdfloat32x3 operator/(float y) const { return vdfloat32x3 { _mm_div_ps(v, _mm_set1_ps(y)) }; }
	};

	[[nodiscard]] inline vdfloat32x3 operator+(float x, vdfloat32x3 y) { return vdfloat32x3 { _mm_add_ps(_mm_set1_ps(x), y.v) }; }
	[[nodiscard]] inline vdfloat32x3 operator-(float x, vdfloat32x3 y) { return vdfloat32x3 { _mm_sub_ps(_mm_set1_ps(x), y.v) }; }
	[[nodiscard]] inline vdfloat32x3 operator*(float x, vdfloat32x3 y) { return vdfloat32x3 { _mm_mul_ps(_mm_set1_ps(x), y.v) }; }
	[[nodiscard]] inline vdfloat32x3 operator/(float x, vdfloat32x3 y) { return vdfloat32x3 { _mm_div_ps(_mm_set1_ps(x), y.v) }; }

	inline vdfloat32x3& operator+=(vdfloat32x3& x, vdfloat32x3 y) { x.v = _mm_add_ps(x.v, y.v); return x; }
	inline vdfloat32x3& operator-=(vdfloat32x3& x, vdfloat32x3 y) { x.v = _mm_sub_ps(x.v, y.v); return x; }
	inline vdfloat32x3& operator*=(vdfloat32x3& x, vdfloat32x3 y) { x.v = _mm_mul_ps(x.v, y.v); return x; }
	inline vdfloat32x3& operator/=(vdfloat32x3& x, vdfloat32x3 y) { x.v = _mm_div_ps(x.v, y.v); return x; }

	inline vdfloat32x3& operator+=(vdfloat32x3& x, float y) { x.v = _mm_add_ps(x.v, _mm_set1_ps(y)); return x; }
	inline vdfloat32x3& operator-=(vdfloat32x3& x, float y) { x.v = _mm_sub_ps(x.v, _mm_set1_ps(y)); return x; }
	inline vdfloat32x3& operator*=(vdfloat32x3& x, float y) { x.v = _mm_mul_ps(x.v, _mm_set1_ps(y)); return x; }
	inline vdfloat32x3& operator/=(vdfloat32x3& x, float y) { x.v = _mm_div_ps(x.v, _mm_set1_ps(y)); return x; }

	[[nodiscard]] inline bool operator==(vdfloat32x3 x, vdfloat32x3 y) { return (_mm_movemask_ps(_mm_cmpneq_ps(x.v, y.v)) & 7) == 0; }
	[[nodiscard]] inline bool operator!=(vdfloat32x3 x, vdfloat32x3 y) { return (_mm_movemask_ps(_mm_cmpneq_ps(x.v, y.v)) & 7) != 0; }

	[[nodiscard]] inline vdmask32x3 cmplt(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { _mm_cmplt_ps(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x3 cmple(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { _mm_cmple_ps(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x3 cmpgt(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { _mm_cmpgt_ps(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x3 cmpge(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { _mm_cmpge_ps(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x3 cmpeq(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { _mm_cmpeq_ps(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x3 cmpne(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { _mm_cmpneq_ps(x.v, y.v) }; }

	struct vdfloat32x4 {
		__m128 v;

		[[nodiscard]] static vdfloat32x4 zero() {
			return vdfloat32x4 { _mm_setzero_ps() };
		}

		[[nodiscard]] static vdfloat32x4 set1(float x) {
			return vdfloat32x4 { _mm_set1_ps(x) };
		}

		[[nodiscard]] static vdfloat32x4 set(float x, float y, float z, float w) {
			return vdfloat32x4 { _mm_set_ps(w, z, y, x) };
		}

		[[nodiscard]] static vdfloat32x4 set(vdfloat32x3 xyz, float w) {
			return vdfloat32x4 { _mm_shuffle_ps(xyz.v, _mm_move_ss(xyz.v, _mm_set1_ps(w)), 0b00100100) };
		}

		[[nodiscard]] static vdfloat32x4 unpacku8(uint32 v8) {
			__m128i zero = _mm_setzero_si128();

			return vdfloat32x4 {
				_mm_cvtepi32_ps(_mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)v8), zero), zero))
			};
		}

		[[nodiscard]] float x() const { return _mm_cvtss_f32(v); }
		[[nodiscard]] float y() const { return _mm_cvtss_f32(_mm_shuffle_ps(v, v, 0x55)); }
		[[nodiscard]] float z() const { return _mm_cvtss_f32(_mm_shuffle_ps(v, v, 0xAA)); }
		[[nodiscard]] float w() const { return _mm_cvtss_f32(_mm_shuffle_ps(v, v, 0xFF)); }
		[[nodiscard]] vdfloat32x3 xyz() const { return vdfloat32x3 { v }; }

		[[nodiscard]] vdfloat32x4 operator+() const { return *this; }
		[[nodiscard]] vdfloat32x4 operator-() const { return vdfloat32x4 { _mm_sub_ps(_mm_setzero_ps(), v) }; }

		[[nodiscard]] vdfloat32x4 operator+(vdfloat32x4 y) const { return vdfloat32x4 { _mm_add_ps(v, y.v) }; }
		[[nodiscard]] vdfloat32x4 operator-(vdfloat32x4 y) const { return vdfloat32x4 { _mm_sub_ps(v, y.v) }; }
		[[nodiscard]] vdfloat32x4 operator*(vdfloat32x4 y) const { return vdfloat32x4 { _mm_mul_ps(v, y.v) }; }
		[[nodiscard]] vdfloat32x4 operator/(vdfloat32x4 y) const { return vdfloat32x4 { _mm_div_ps(v, y.v) }; }

		[[nodiscard]] vdfloat32x4 operator+(float y) const { return vdfloat32x4 { _mm_add_ps(v, _mm_set1_ps(y)) }; }
		[[nodiscard]] vdfloat32x4 operator-(float y) const { return vdfloat32x4 { _mm_sub_ps(v, _mm_set1_ps(y)) }; }
		[[nodiscard]] vdfloat32x4 operator*(float y) const { return vdfloat32x4 { _mm_mul_ps(v, _mm_set1_ps(y)) }; }
		[[nodiscard]] vdfloat32x4 operator/(float y) const { return vdfloat32x4 { _mm_div_ps(v, _mm_set1_ps(y)) }; }
	};

	[[nodiscard]] inline vdfloat32x4 operator+(float x, vdfloat32x4 y) { return vdfloat32x4 { _mm_add_ps(_mm_set1_ps(x), y.v) }; }
	[[nodiscard]] inline vdfloat32x4 operator-(float x, vdfloat32x4 y) { return vdfloat32x4 { _mm_sub_ps(_mm_set1_ps(x), y.v) }; }
	[[nodiscard]] inline vdfloat32x4 operator*(float x, vdfloat32x4 y) { return vdfloat32x4 { _mm_mul_ps(_mm_set1_ps(x), y.v) }; }
	[[nodiscard]] inline vdfloat32x4 operator/(float x, vdfloat32x4 y) { return vdfloat32x4 { _mm_div_ps(_mm_set1_ps(x), y.v) }; }

	inline vdfloat32x4& operator+=(vdfloat32x4& x, vdfloat32x4 y) { x.v = _mm_add_ps(x.v, y.v); return x; }
	inline vdfloat32x4& operator-=(vdfloat32x4& x, vdfloat32x4 y) { x.v = _mm_sub_ps(x.v, y.v); return x; }
	inline vdfloat32x4& operator*=(vdfloat32x4& x, vdfloat32x4 y) { x.v = _mm_mul_ps(x.v, y.v); return x; }
	inline vdfloat32x4& operator/=(vdfloat32x4& x, vdfloat32x4 y) { x.v = _mm_div_ps(x.v, y.v); return x; }

	inline vdfloat32x4& operator+=(vdfloat32x4& x, float y) { x.v = _mm_add_ps(x.v, _mm_set1_ps(y)); return x; }
	inline vdfloat32x4& operator-=(vdfloat32x4& x, float y) { x.v = _mm_sub_ps(x.v, _mm_set1_ps(y)); return x; }
	inline vdfloat32x4& operator*=(vdfloat32x4& x, float y) { x.v = _mm_mul_ps(x.v, _mm_set1_ps(y)); return x; }
	inline vdfloat32x4& operator/=(vdfloat32x4& x, float y) { x.v = _mm_div_ps(x.v, _mm_set1_ps(y)); return x; }

	[[nodiscard]] inline bool operator==(vdfloat32x4 x, vdfloat32x4 y) { return _mm_movemask_ps(_mm_cmpneq_ps(x.v, y.v)) == 0; }
	[[nodiscard]] inline bool operator!=(vdfloat32x4 x, vdfloat32x4 y) { return _mm_movemask_ps(_mm_cmpneq_ps(x.v, y.v)) != 0; }

	[[nodiscard]] inline vdmask32x4 cmplt(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { _mm_cmplt_ps(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x4 cmple(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { _mm_cmple_ps(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x4 cmpgt(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { _mm_cmpgt_ps(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x4 cmpge(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { _mm_cmpge_ps(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x4 cmpeq(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { _mm_cmpeq_ps(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x4 cmpne(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { _mm_cmpneq_ps(x.v, y.v) }; }

	///////////////////////////////////////////////////////////////////////

	struct vdfloat32x3x3 {
		vdfloat32x3 x, y, z;
	};

	[[nodiscard]] inline vdfloat32x3x3 loadu(const vdfloat3x3& m) {
		__m128 v3 = _mm_loadu_ps(&m.y.z);

		return vdfloat32x3x3 {
			{ _mm_loadu_ps(&m.x.x) },
			{ _mm_loadu_ps(&m.y.x) },
			{ _mm_shuffle_ps(v3, v3, 0b11'11'10'01) }
		};
	}

	[[nodiscard]] inline vdfloat32x3 mul(vdfloat32x3 a, const vdfloat32x3x3& b) {
		return vdfloat32x3 {
			_mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(a.v, a.v, 0x00), b.x.v), _mm_mul_ps(_mm_shuffle_ps(a.v, a.v, 0x55), b.y.v)), _mm_mul_ps(_mm_shuffle_ps(a.v, a.v, 0xAA), b.z.v))
		};
	}

	[[nodiscard]] inline vdfloat32x3x3 mul(vdfloat32x3x3 a, vdfloat32x3x3 b) {
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

	[[nodiscard]] inline vdfloat32x3 mul1(vdfloat32x3 a, const vdfloat32x3x4& b) {
		return a.x() * b.x
			+ a.y() * b.y
			+ a.z() * b.z
			+ b.w;
	}

	///////////////////////////////////////////////////////////////////////

	struct vdint32x4 {
		__m128i v;

		static vdint32x4 set(sint32 x, sint32 y, sint32 z, sint32 w) {
			return vdint32x4 { _mm_set_epi32(w, z, y, x) };
		}

		[[nodiscard]] bool operator==(vdint32x4 y) const {
			return _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(v, y.v))) == 15;
		}

		[[nodiscard]] bool operator!=(vdint32x4 y) const {
			return _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(v, y.v))) != 15;
		}
	};

	///////////////////////////////////////////////////////////////////////

	[[nodiscard]] inline bool all_bool(const vdmask32x3& v) {
		return (_mm_movemask_ps(v.v) & 7) == 7;
	}

	[[nodiscard]] inline bool all_bool(const vdmask32x4& v) {
		return _mm_movemask_ps(v.v) == 15;
	}

	[[nodiscard]] inline vdfloat32x4 loadu(const vdfloat4& v) {
		return vdfloat32x4 { _mm_loadu_ps((const float *)&v) };
	}

	inline void storeu(void *dst, vdint32x4 v) {
		_mm_storeu_si128((__m128i *)dst, v.v);
	}

	[[nodiscard]] inline vdfloat32x3 select(vdmask32x3 mask, vdfloat32x3 x, vdfloat32x3 y) {
		return vdfloat32x3 { _mm_or_ps(_mm_and_ps(mask.v, x.v), _mm_andnot_ps(mask.v, y.v)) };
	}

	[[nodiscard]] inline vdfloat32x4 select(vdmask32x4 mask, vdfloat32x4 x, vdfloat32x4 y) {
		return vdfloat32x4 { _mm_or_ps(_mm_and_ps(mask.v, x.v), _mm_andnot_ps(mask.v, y.v)) };
	}

	[[nodiscard]]
	inline vdfloat32x4 nonzeromask(vdfloat32x4 x, vdmask32x4 mask) {
		return vdfloat32x4 { _mm_and_ps(x.v, mask.v) };
	}

	[[nodiscard]] inline vdfloat32x3 abs(vdfloat32x3 x) {
		return vdfloat32x3 { _mm_max_ps(x.v, _mm_sub_ps(_mm_setzero_ps(), x.v)) };
	}

	[[nodiscard]] inline vdfloat32x4 abs(vdfloat32x4 x) {
		return vdfloat32x4 { _mm_max_ps(x.v, _mm_sub_ps(_mm_setzero_ps(), x.v)) };
	}

	[[nodiscard]] inline float maxcomponent(vdfloat32x3 x) {
		return _mm_cvtss_f32(_mm_max_ps(_mm_max_ps(x.v, _mm_shuffle_ps(x.v, x.v, 0x55)), _mm_shuffle_ps(x.v, x.v, 0xAA)));
	}

	[[nodiscard]] inline float maxcomponent(vdfloat32x4 x) {
		__m128 t = _mm_max_ps(x.v, _mm_shuffle_ps(x.v, x.v, 0x4E));

		return _mm_cvtss_f32(_mm_max_ps(t, _mm_shuffle_ps(t, t, 0xB1)));
	}

	[[nodiscard]] inline float dot(vdfloat32x3 x, vdfloat32x3 y) {
		__m128 t = _mm_mul_ps(x.v, y.v);

		return _mm_cvtss_f32(_mm_add_ps(_mm_add_ps(t, _mm_shuffle_ps(t, t, 0x55)), _mm_shuffle_ps(t, t, 0xAA)));
	}

	[[nodiscard]] inline float dot(vdfloat32x4 x, vdfloat32x4 y) {
		__m128 t = _mm_mul_ps(x.v, y.v);

		return _mm_cvtss_f32(
			_mm_add_ps(
				_mm_add_ps(t, _mm_shuffle_ps(t, t, 0x55)),
				_mm_add_ps(_mm_shuffle_ps(t, t, 0xAA), _mm_shuffle_ps(t, t, 0xFF))
			)
		);
	}

	[[nodiscard]] inline vdfloat32x3 sqrt(vdfloat32x3 x) {
		return vdfloat32x3 { _mm_sqrt_ps(x.v) };
	}

	[[nodiscard]] inline vdfloat32x4 sqrt(vdfloat32x4 x) {
		return vdfloat32x4 { _mm_sqrt_ps(x.v) };
	}

	[[nodiscard]] inline vdfloat32x3 lerp(vdfloat32x3 x, vdfloat32x3 y, float f) {
		return vdfloat32x3 { _mm_add_ps(_mm_mul_ps(x.v, _mm_set1_ps(1.0f - f)), _mm_mul_ps(y.v, _mm_set1_ps(f))) };
	}

	[[nodiscard]] inline vdfloat32x3 lerp(vdfloat32x3 x, vdfloat32x3 y, vdfloat32x3 f) {
		return vdfloat32x3 { _mm_add_ps(_mm_mul_ps(x.v, _mm_sub_ps(_mm_set1_ps(1.0f), f.v)), _mm_mul_ps(y.v, f.v)) };
	}

	[[nodiscard]] inline vdfloat32x4 rcp(vdfloat32x4 x) {
		return vdfloat32x4 { _mm_div_ps(_mm_set1_ps(1.0f), x.v) };
	}

	[[nodiscard]] inline vdfloat32x3 rcpest(vdfloat32x3 x) {
		return vdfloat32x3 { _mm_rcp_ps(x.v) };
	}

	[[nodiscard]] inline vdfloat32x4 rcpest(vdfloat32x4 x) {
		return vdfloat32x4 { _mm_rcp_ps(x.v) };
	}

	[[nodiscard]] inline vdfloat32x4 rsqrt(vdfloat32x4 x) {
		return vdfloat32x4 { _mm_div_ps(_mm_set1_ps(1.0f), _mm_sqrt_ps(x.v)) };
	}

	[[nodiscard]] inline vdfloat32x3 rsqrtest(vdfloat32x3 x) {
		return vdfloat32x3 { _mm_rsqrt_ps(x.v) };
	}

	[[nodiscard]] inline vdfloat32x4 rsqrtest(vdfloat32x4 x) {
		return vdfloat32x4 { _mm_rsqrt_ps(x.v) };
	}

	template<unsigned xsel, unsigned ysel, unsigned zsel>
	[[nodiscard]] inline vdfloat32x3 permute(vdfloat32x3 x) {
		static_assert(xsel < 3 && ysel < 3 && zsel < 3);

		return vdfloat32x3 { _mm_shuffle_ps(x.v, x.v, xsel + ysel*4 + zsel*(16+64)) };
	}

	template<unsigned xsel, unsigned ysel, unsigned zsel, unsigned wsel>
	[[nodiscard]] inline vdfloat32x4 permute(vdfloat32x4 x) {
		static_assert((xsel | ysel | zsel | wsel) < 4);

		return vdfloat32x4 { _mm_shuffle_ps(x.v, x.v, xsel + ysel*4 + zsel*16 + wsel*64) };
	}

	[[nodiscard]] inline vdfloat32x3 min(vdfloat32x3 x, vdfloat32x3 y) { return vdfloat32x3 { _mm_min_ps(x.v, y.v) }; }
	[[nodiscard]] inline vdfloat32x4 min(vdfloat32x4 x, vdfloat32x4 y) { return vdfloat32x4 { _mm_min_ps(x.v, y.v) }; }

	[[nodiscard]] inline vdfloat32x3 max0(vdfloat32x3 x) { return vdfloat32x3 { _mm_max_ps(x.v, _mm_setzero_ps()) }; }
	[[nodiscard]] inline vdfloat32x4 max0(vdfloat32x4 x) { return vdfloat32x4 { _mm_max_ps(x.v, _mm_setzero_ps()) }; }

	[[nodiscard]] inline vdfloat32x3 max(vdfloat32x3 x, vdfloat32x3 y) { return vdfloat32x3 { _mm_max_ps(x.v, y.v) }; }
	[[nodiscard]] inline vdfloat32x4 max(vdfloat32x4 x, vdfloat32x4 y) { return vdfloat32x4 { _mm_max_ps(x.v, y.v) }; }

	[[nodiscard]] inline vdfloat32x3 clamp(vdfloat32x3 x, vdfloat32x3 mn, vdfloat32x3 mx) { return min(max(x, mn), mx); }
	[[nodiscard]] inline vdfloat32x4 clamp(vdfloat32x4 x, vdfloat32x4 mn, vdfloat32x4 mx) { return min(max(x, mn), mx); }

	[[nodiscard]] inline vdfloat32x3 saturate(vdfloat32x3 x) { return clamp(x, vdfloat32x3::set1(0), vdfloat32x3::set1(1)); }
	[[nodiscard]] inline vdfloat32x4 saturate(vdfloat32x4 x) { return clamp(x, vdfloat32x4::set1(0), vdfloat32x4::set1(1)); }

	[[nodiscard]] inline vdfloat32x3 pow(vdfloat32x3 x, float y) {
		return vdfloat32x3::set(
			powf(x.x(), y),
			powf(x.y(), y),
			powf(x.z(), y)
		);
	}

	[[nodiscard]] inline vdfloat32x4 pow(vdfloat32x4 x, float y) {
		return vdfloat32x4::set(
			powf(x.x(), y),
			powf(x.y(), y),
			powf(x.z(), y),
			powf(x.w(), y)
		);
	}

	[[nodiscard]] inline uint32 packus8(vdfloat32x3 x) {
		__m128i y = _mm_cvtps_epi32(x.v);

		y = _mm_packs_epi32(y, y);
		
		return (uint32)_mm_cvtsi128_si32(_mm_packus_epi16(y, y)) & 0xFFFFFF;
	}

	[[nodiscard]] inline uint32 packus8(vdfloat32x4 x) {
		__m128i y = _mm_cvtps_epi32(x.v);

		y = _mm_packs_epi32(y, y);
		
		return (uint32)_mm_cvtsi128_si32(_mm_packus_epi16(y, y));
	}

	[[nodiscard]] inline vdint32x4 ceilint(vdfloat32x4 x) {
		__m128i ix = _mm_cvttps_epi32(x.v);

		return vdint32x4 { _mm_sub_epi32(ix, _mm_castps_si128(_mm_cmplt_ps(_mm_cvtepi32_ps(ix), x.v))) };
	}
}

using nsVDVecMath::vdmask32x3;
using nsVDVecMath::vdmask32x4;
using nsVDVecMath::vdfloat32x3;
using nsVDVecMath::vdfloat32x4;
using nsVDVecMath::vdint32x4;

#endif
