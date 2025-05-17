//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 2024 Avery Lee, All Rights Reserved.
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

#ifndef f_VD2_SYSTEM_VECMATH_NEON_H
#define f_VD2_SYSTEM_VECMATH_NEON_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/vectors.h>
#include <vd2/system/win32/intrin.h>
#include <arm_neon.h>

namespace nsVDVecMath {
	struct vdmask32x3 {
		uint32x4_t v;

		static vdmask32x3 set(bool x, bool y, bool z) {
			return vdmask32x3 {
				vreinterpretq_u32_s32(vmovl_s16(vreinterpret_s16_u64(vmov_n_u64(
					(x ? INT64_C(0x0000'0000'0000'FFFF) : 0) +
					(y ? INT64_C(0x0000'0000'FFFF'0000) : 0) +
					(z ? INT64_C(0x0000'FFFF'0000'0000) : 0)
				))))
			};
		}
	};

	[[nodiscard]] inline bool operator==(vdmask32x3 a, vdmask32x3 b) { return ((vget_lane_u64(vreinterpret_u64_u16(vmovn_u32(vceqq_u32(a.v, b.v))), 0) | ((uint64_t)0xFFFF << 48)) + 1) == 0; }
	[[nodiscard]] inline bool operator!=(vdmask32x3 a, vdmask32x3 b) { return ((vget_lane_u64(vreinterpret_u64_u16(vmovn_u32(vceqq_u32(a.v, b.v))), 0) | ((uint64_t)0xFFFF << 48)) + 1) != 0; }

	[[nodiscard]] inline vdmask32x3 operator~(vdmask32x3 a) { return vdmask32x3 { vmvnq_u32(a.v) }; }
	[[nodiscard]] inline vdmask32x3 operator&(vdmask32x3 a, vdmask32x3 b) { return vdmask32x3 { vandq_u32(a.v, b.v) }; }
	[[nodiscard]] inline vdmask32x3 operator|(vdmask32x3 a, vdmask32x3 b) { return vdmask32x3 { vorrq_u32(a.v, b.v) }; }
	[[nodiscard]] inline vdmask32x3 operator^(vdmask32x3 a, vdmask32x3 b) { return vdmask32x3 { veorq_u32(a.v, b.v) }; }

	[[nodiscard]] inline bool all_bool(const vdmask32x3& v) {
		return ((vget_lane_u64(vreinterpret_u64_u16(vmovn_u32(v.v)), 0) | ((uint64_t)0xFFFF << 48)) + 1) == 0;
	}

	///////////////////////////////////////////////////////////////////////

	struct vdmask32x4 {
		uint32x4_t v;

		static vdmask32x4 set(bool x, bool y, bool z, bool w) {
			return vdmask32x4 {
				vreinterpretq_u32_s32(vmovl_s16(vreinterpret_s16_u64(vmov_n_u64(
					(x ? INT64_C(0x0000'0000'0000'FFFF) : 0) +
					(y ? INT64_C(0x0000'0000'FFFF'0000) : 0) +
					(z ? INT64_C(0x0000'FFFF'0000'0000) : 0) +
					(w ? INT64_C(0xFFFF'0000'0000'0000) : 0)
				))))
			};
		}
	};

	[[nodiscard]] inline bool operator==(vdmask32x4 a, vdmask32x4 b) { return vget_lane_u64(vreinterpret_u64_u16(vmovn_u32(vceqq_u32(a.v, b.v))), 0) + 1 == 0; }
	[[nodiscard]] inline bool operator!=(vdmask32x4 a, vdmask32x4 b) { return vget_lane_u64(vreinterpret_u64_u16(vmovn_u32(vceqq_u32(a.v, b.v))), 0) + 1 != 0; }

	[[nodiscard]] inline vdmask32x4 operator~(vdmask32x4 a) { return vdmask32x4 { vmvnq_u32(a.v) }; }
	[[nodiscard]] inline vdmask32x4 operator&(vdmask32x4 a, vdmask32x4 b) { return vdmask32x4 { vandq_u32(a.v, b.v) }; }
	[[nodiscard]] inline vdmask32x4 operator|(vdmask32x4 a, vdmask32x4 b) { return vdmask32x4 { vorrq_u32(a.v, b.v) }; }
	[[nodiscard]] inline vdmask32x4 operator^(vdmask32x4 a, vdmask32x4 b) { return vdmask32x4 { veorq_u32(a.v, b.v) }; }

	[[nodiscard]] inline bool all_bool(const vdmask32x4& v) {
		return (vget_lane_u64(vreinterpret_u64_u16(vmovn_u32(v.v)), 0) + 1) == 0;
	}

	///////////////////////////////////////////////////////////////////////

	struct vdfloat32x3 {
		float32x4_t v;

		[[nodiscard]] static vdfloat32x3 zero() {
			return vdfloat32x3 { vdupq_n_f32(0) };
		}

		[[nodiscard]] static vdfloat32x3 set1(float x) {
			return vdfloat32x3 { vdupq_n_f32(x) };
		}

		[[nodiscard]] static vdfloat32x3 set(float x, float y, float z) {
			return vdfloat32x3 { vsetq_lane_f32(x, vsetq_lane_f32(y, vmovq_n_f32(z), 1), 0) };
		}

		[[nodiscard]] float x() const { return vgetq_lane_f32(v, 0); }
		[[nodiscard]] float y() const { return vgetq_lane_f32(v, 1); }
		[[nodiscard]] float z() const { return vgetq_lane_f32(v, 2); }

		[[nodiscard]] bool operator==(vdfloat32x3 y) const { return (vget_lane_u64(vreinterpret_u64_u16(vqmovn_u32(vceqq_f32(v, y.v))), 0) | ((uint64_t)0xFFFF << 48)) + 1 == 0; }
		[[nodiscard]] bool operator!=(vdfloat32x3 y) const { return (vget_lane_u64(vreinterpret_u64_u16(vqmovn_u32(vceqq_f32(v, y.v))), 0) | ((uint64_t)0xFFFF << 48)) + 1 != 0; }

		[[nodiscard]] vdfloat32x3 operator+() const { return *this; }
		[[nodiscard]] vdfloat32x3 operator-() const { return vdfloat32x3 { vnegq_f32(v) }; }

		[[nodiscard]] vdfloat32x3 operator+(vdfloat32x3 y) const { return vdfloat32x3 { vaddq_f32(v, y.v) }; }
		[[nodiscard]] vdfloat32x3 operator-(vdfloat32x3 y) const { return vdfloat32x3 { vsubq_f32(v, y.v) }; }
		[[nodiscard]] vdfloat32x3 operator*(vdfloat32x3 y) const { return vdfloat32x3 { vmulq_f32(v, y.v) }; }
		[[nodiscard]] vdfloat32x3 operator/(vdfloat32x3 y) const { return vdfloat32x3 { vdivq_f32(v, y.v) }; }

		[[nodiscard]] vdfloat32x3 operator+(float y) const { return vdfloat32x3 { vaddq_f32(v, vdupq_n_f32(y)) }; }
		[[nodiscard]] vdfloat32x3 operator-(float y) const { return vdfloat32x3 { vsubq_f32(v, vdupq_n_f32(y)) }; }
		[[nodiscard]] vdfloat32x3 operator*(float y) const { return vdfloat32x3 { vmulq_f32(v, vdupq_n_f32(y)) }; }
		[[nodiscard]] vdfloat32x3 operator/(float y) const { return vdfloat32x3 { vdivq_f32(v, vdupq_n_f32(y)) }; }

		inline vdfloat32x3& operator+=(vdfloat32x3 y) { v = vaddq_f32(v, y.v); return *this; }
		inline vdfloat32x3& operator-=(vdfloat32x3 y) { v = vsubq_f32(v, y.v); return *this; }
		inline vdfloat32x3& operator*=(vdfloat32x3 y) { v = vmulq_f32(v, y.v); return *this; }
		inline vdfloat32x3& operator/=(vdfloat32x3 y) { v = vdivq_f32(v, y.v); return *this; }

		inline vdfloat32x3& operator+=(float y) { v = vaddq_f32(v, vdupq_n_f32(y)); return *this; }
		inline vdfloat32x3& operator-=(float y) { v = vsubq_f32(v, vdupq_n_f32(y)); return *this; }
		inline vdfloat32x3& operator*=(float y) { v = vmulq_f32(v, vdupq_n_f32(y)); return *this; }
		inline vdfloat32x3& operator/=(float y) { v = vdivq_f32(v, vdupq_n_f32(y)); return *this; }
	};

	[[nodiscard]] inline vdfloat32x3 operator+(float x, vdfloat32x3 y) { return vdfloat32x3 { vaddq_f32(vdupq_n_f32(x), y.v) }; }
	[[nodiscard]] inline vdfloat32x3 operator-(float x, vdfloat32x3 y) { return vdfloat32x3 { vsubq_f32(vdupq_n_f32(x), y.v) }; }
	[[nodiscard]] inline vdfloat32x3 operator*(float x, vdfloat32x3 y) { return vdfloat32x3 { vmulq_f32(vdupq_n_f32(x), y.v) }; }
	[[nodiscard]] inline vdfloat32x3 operator/(float x, vdfloat32x3 y) { return vdfloat32x3 { vdivq_f32(vdupq_n_f32(x), y.v) }; }

	[[nodiscard]] inline vdmask32x3 cmplt(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { vcltq_f32(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x3 cmple(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { vcleq_f32(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x3 cmpgt(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { vcgtq_f32(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x3 cmpge(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { vcgeq_f32(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x3 cmpeq(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { vceqq_f32(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x3 cmpne(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { vmvnq_u32(vceqq_f32(x.v, y.v)) }; }

	struct vdfloat32x4 {
		float32x4_t v;

		[[nodiscard]] static vdfloat32x4 zero() {
			return vdfloat32x4 { vdupq_n_f32(0) };
		}

		[[nodiscard]] static vdfloat32x4 set1(float x) {
			return vdfloat32x4 { vdupq_n_f32(x) };
		}

		[[nodiscard]] static vdfloat32x4 set(float x, float y, float z, float w) {
			const float v[4] { x, y, z, w };

			return vdfloat32x4 { vld1q_f32(v) };
		}

		[[nodiscard]] static vdfloat32x4 set(vdfloat32x3 xyz, float w) {
			return vdfloat32x4 { vsetq_lane_f32(w, xyz.v, 3) };
		}

		[[nodiscard]] static vdfloat32x4 unpacku8(uint32 v8) {
			return vdfloat32x4 {
				vcvtq_f32_u32(vmovl_u16(vget_low_u32(vmovl_u8(vreinterpret_u8_u32(vdup_n_u32(v8))))))
			};
		}

		[[nodiscard]] float x() const { return vgetq_lane_f32(v, 0); }
		[[nodiscard]] float y() const { return vgetq_lane_f32(v, 1); }
		[[nodiscard]] float z() const { return vgetq_lane_f32(v, 2); }
		[[nodiscard]] float w() const { return vgetq_lane_f32(v, 3); }
		[[nodiscard]] vdfloat32x3 xyz() const { return vdfloat32x3 { v }; }

		[[nodiscard]] bool operator==(vdfloat32x4 y) const { return vget_lane_u64(vreinterpret_u64_u16(vqmovn_u32(vceqq_f32(v, y.v))), 0) + 1 == 0; }
		[[nodiscard]] bool operator!=(vdfloat32x4 y) const { return vget_lane_u64(vreinterpret_u64_u16(vqmovn_u32(vceqq_f32(v, y.v))), 0) + 1 != 0; }

		[[nodiscard]] vdfloat32x4 operator+() const { return *this; }
		[[nodiscard]] vdfloat32x4 operator-() const { return vdfloat32x4 { vnegq_f32(v) }; }

		[[nodiscard]] vdfloat32x4 operator+(vdfloat32x4 y) const { return vdfloat32x4 { vaddq_f32(v, y.v) }; }
		[[nodiscard]] vdfloat32x4 operator-(vdfloat32x4 y) const { return vdfloat32x4 { vsubq_f32(v, y.v) }; }
		[[nodiscard]] vdfloat32x4 operator*(vdfloat32x4 y) const { return vdfloat32x4 { vmulq_f32(v, y.v) }; }
		[[nodiscard]] vdfloat32x4 operator/(vdfloat32x4 y) const { return vdfloat32x4 { vdivq_f32(v, y.v) }; }

		[[nodiscard]] vdfloat32x4 operator+(float y) const { return vdfloat32x4 { vaddq_f32(v, vdupq_n_f32(y)) }; }
		[[nodiscard]] vdfloat32x4 operator-(float y) const { return vdfloat32x4 { vsubq_f32(v, vdupq_n_f32(y)) }; }
		[[nodiscard]] vdfloat32x4 operator*(float y) const { return vdfloat32x4 { vmulq_f32(v, vdupq_n_f32(y)) }; }
		[[nodiscard]] vdfloat32x4 operator/(float y) const { return vdfloat32x4 { vdivq_f32(v, vdupq_n_f32(y)) }; }

		vdfloat32x4& operator+=(vdfloat32x4 y) { v = vaddq_f32(v, y.v); return *this; }
		vdfloat32x4& operator-=(vdfloat32x4 y) { v = vsubq_f32(v, y.v); return *this; }
		vdfloat32x4& operator*=(vdfloat32x4 y) { v = vmulq_f32(v, y.v); return *this; }
		vdfloat32x4& operator/=(vdfloat32x4 y) { v = vdivq_f32(v, y.v); return *this; }

		vdfloat32x4& operator+=(float y) { v = vaddq_f32(v, vdupq_n_f32(y)); return *this; }
		vdfloat32x4& operator-=(float y) { v = vsubq_f32(v, vdupq_n_f32(y)); return *this; }
		vdfloat32x4& operator*=(float y) { v = vmulq_f32(v, vdupq_n_f32(y)); return *this; }
		vdfloat32x4& operator/=(float y) { v = vdivq_f32(v, vdupq_n_f32(y)); return *this; }
	};

	[[nodiscard]] inline vdfloat32x4 operator+(float x, vdfloat32x4 y) { return vdfloat32x4 { vaddq_f32(vdupq_n_f32(x), y.v) }; }
	[[nodiscard]] inline vdfloat32x4 operator-(float x, vdfloat32x4 y) { return vdfloat32x4 { vsubq_f32(vdupq_n_f32(x), y.v) }; }
	[[nodiscard]] inline vdfloat32x4 operator*(float x, vdfloat32x4 y) { return vdfloat32x4 { vmulq_f32(vdupq_n_f32(x), y.v) }; }
	[[nodiscard]] inline vdfloat32x4 operator/(float x, vdfloat32x4 y) { return vdfloat32x4 { vdivq_f32(vdupq_n_f32(x), y.v) }; }

	[[nodiscard]] inline vdmask32x4 cmplt(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { vcltq_f32(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x4 cmple(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { vcleq_f32(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x4 cmpgt(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { vcgtq_f32(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x4 cmpge(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { vcgeq_f32(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x4 cmpeq(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { vceqq_f32(x.v, y.v) }; }
	[[nodiscard]] inline vdmask32x4 cmpne(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { vmvnq_u32(vceqq_f32(x.v, y.v)) }; }

	///////////////////////////////////////////////////////////////////////

	struct vdfloat32x3x3 {
		vdfloat32x3 x, y, z;
	};

	[[nodiscard]] inline vdfloat32x3x3 loadu(const vdfloat3x3& m) {
		float32x4_t v0 = vld1q_f32(&m.x.x);
		float32x4_t v1 = vld1q_f32(&m.y.y);

		return vdfloat32x3x3 {
			{ v0 }, { vextq_f32(v0, v1, 3) }, { vsetq_lane_f32(m.z.z, vextq_f32(v1, v1, 2), 2) }
		};
	}

	[[nodiscard]] inline vdfloat32x3 mul(vdfloat32x3 a, const vdfloat32x3x3& b) {
		return vdfloat32x3 {
			vfmaq_laneq_f32(vfmaq_laneq_f32(vmulq_laneq_f32(b.x.v, a.v, 0), b.y.v, a.v, 1), b.z.v, a.v, 2)
		};
	}

	[[nodiscard]] inline vdfloat32x3x3 mul(vdfloat32x3x3 a, vdfloat32x3x3 b) {
		return vdfloat32x3x3 {
			{ vfmaq_laneq_f32(vfmaq_laneq_f32(vmulq_laneq_f32(b.x.v, a.x.v, 0), b.y.v, a.x.v, 1), b.z.v, a.x.v, 2) },
			{ vfmaq_laneq_f32(vfmaq_laneq_f32(vmulq_laneq_f32(b.x.v, a.y.v, 0), b.y.v, a.y.v, 1), b.z.v, a.y.v, 2) },
			{ vfmaq_laneq_f32(vfmaq_laneq_f32(vmulq_laneq_f32(b.x.v, a.z.v, 0), b.y.v, a.z.v, 1), b.z.v, a.z.v, 2) },
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
		int32x4_t v;

		[[nodiscard]] static vdint32x4 set(sint32 x, sint32 y, sint32 z, sint32 w) {
			const int32_t v[4] { x, y, z, w };

			return vdint32x4 { vld1q_s32(v) };
		}

		[[nodiscard]] bool operator==(vdint32x4 y) const {
			return ((vget_lane_u64(vreinterpret_u64_u16(vmovn_u32(vceqq_u32(v, y.v))), 0) | ((uint64_t)0xFFFF << 48)) + 1) == 0;
		}

		[[nodiscard]] bool operator!=(vdint32x4 y) const {
			return ((vget_lane_u64(vreinterpret_u64_u16(vmovn_u32(vceqq_u32(v, y.v))), 0) | ((uint64_t)0xFFFF << 48)) + 1) != 0;
		}
	};

	///////////////////////////////////////////////////////////////////////

	[[nodiscard]] inline vdfloat32x4 loadu(const vdfloat4& v) {
		return vdfloat32x4 { vld1q_f32(&v.x) };
	}

	inline void storeu(void *dst, vdint32x4 v) {
		vst1q_s32((int32_t *)dst, v.v);
	}

	[[nodiscard]] inline vdfloat32x3 select(vdmask32x3 mask, vdfloat32x3 x, vdfloat32x3 y) {
		return vdfloat32x3 { vbslq_f32(mask.v, x.v, y.v) };
	}

	[[nodiscard]] inline vdfloat32x4 select(vdmask32x4 mask, vdfloat32x4 x, vdfloat32x4 y) {
		return vdfloat32x4 { vbslq_f32(mask.v, x.v, y.v) };
	}

	[[nodiscard]]
	inline vdfloat32x4 nonzeromask(vdfloat32x4 x, vdmask32x4 mask) {
		return vdfloat32x4 { vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(x.v), mask.v)) };
	}

	[[nodiscard]] inline vdfloat32x3 abs(vdfloat32x3 x) {
		return vdfloat32x3 { vabsq_f32(x.v) };
	}

	[[nodiscard]] inline vdfloat32x4 abs(vdfloat32x4 x) {
		return vdfloat32x4 { vabsq_f32(x.v) };
	}

	[[nodiscard]] inline float maxcomponent(vdfloat32x3 x) {
		return vmaxvq_f32(vcopyq_laneq_f32(x.v, 3, x.v, 2));
	}

	[[nodiscard]] inline float maxcomponent(vdfloat32x4 x) {
		return vmaxvq_f32(x.v);
	}

	[[nodiscard]] inline float dot(vdfloat32x3 x, vdfloat32x3 y) {
		return vaddvq_f32(vsetq_lane_f32(0.0f, vmulq_f32(x.v, y.v), 3));
	}

	[[nodiscard]] inline float dot(vdfloat32x4 x, vdfloat32x4 y) {
		return vaddvq_f32(vmulq_f32(x.v, y.v));
	}

	[[nodiscard]] inline vdfloat32x3 sqrt(vdfloat32x3 x) {
		return vdfloat32x3 { vsqrtq_f32(x.v) };
	}

	[[nodiscard]] inline vdfloat32x4 sqrt(vdfloat32x4 x) {
		return vdfloat32x4 { vsqrtq_f32(x.v) };
	}

	[[nodiscard]] inline vdfloat32x3 lerp(vdfloat32x3 x, vdfloat32x3 y, float f) {
		return vdfloat32x3 { vfmaq_f32(vfmsq_f32(x.v, x.v, vdupq_n_f32(f)), y.v, vdupq_n_f32(f)) };
	}

	[[nodiscard]] inline vdfloat32x3 lerp(vdfloat32x3 x, vdfloat32x3 y, vdfloat32x3 f) {
		return vdfloat32x3 { vfmaq_f32(vfmsq_f32(x.v, x.v, f.v), y.v, f.v) };
	}

	[[nodiscard]] inline vdfloat32x4 rcp(vdfloat32x4 x) {
		return vdfloat32x4 { vdivq_f32(vdupq_n_f32(1.0f), x.v) };
	}

	[[nodiscard]] inline vdfloat32x3 rcpest(vdfloat32x3 x) {
		return vdfloat32x3 { vrecpeq_f32(x.v) };
	}

	[[nodiscard]] inline vdfloat32x4 rcpest(vdfloat32x4 x) {
		return vdfloat32x4 { vrecpeq_f32(x.v) };
	}

	[[nodiscard]] inline vdfloat32x4 rsqrt(vdfloat32x4 x) {
		return vdfloat32x4 { vdivq_f32(vdupq_n_f32(1.0f), vsqrtq_f32(x.v)) };
	}

	[[nodiscard]] inline vdfloat32x3 rsqrtest(vdfloat32x3 x) {
		return vdfloat32x3 { vrsqrteq_f32(x.v) };
	}

	[[nodiscard]] inline vdfloat32x4 rsqrtest(vdfloat32x4 x) {
		return vdfloat32x4 { vrsqrteq_f32(x.v) };
	}

	template<unsigned xsel, unsigned ysel, unsigned zsel>
	[[nodiscard]] inline vdfloat32x3 permute(vdfloat32x3 x) {
		static_assert(xsel < 3 && ysel < 3 && zsel < 3);

		return vdfloat32x3 {
			vcopyq_laneq_f32(
				vcopyq_laneq_f32(
					vcopyq_laneq_f32(x.v, 0, x.v, xsel),
					1, x.v, ysel
				),
				2, x.v, zsel
			)
		};
	}

	template<unsigned xsel, unsigned ysel, unsigned zsel, unsigned wsel>
	[[nodiscard]] inline vdfloat32x4 permute(vdfloat32x4 x) {
		static_assert((xsel | ysel | zsel | wsel) < 4);

		return vdfloat32x4 {
			vcopyq_laneq_f32(
				vcopyq_laneq_f32(
					vcopyq_laneq_f32(
						vcopyq_laneq_f32(x.v, 0, x.v, xsel),
						1, x.v, ysel
					),
					2, x.v, zsel
				),
				3, x.v, wsel
			)
		};
	}

	[[nodiscard]] inline vdfloat32x3 min(vdfloat32x3 x, vdfloat32x3 y) { return vdfloat32x3 { vminq_f32(x.v, y.v) }; }
	[[nodiscard]] inline vdfloat32x4 min(vdfloat32x4 x, vdfloat32x4 y) { return vdfloat32x4 { vminq_f32(x.v, y.v) }; }

	[[nodiscard]] inline vdfloat32x3 max0(vdfloat32x3 x) { return vdfloat32x3 { vmaxq_f32(x.v, vdupq_n_f32(0)) }; }
	[[nodiscard]] inline vdfloat32x4 max0(vdfloat32x4 x) { return vdfloat32x4 { vmaxq_f32(x.v, vdupq_n_f32(0)) }; }

	[[nodiscard]] inline vdfloat32x3 max(vdfloat32x3 x, vdfloat32x3 y) { return vdfloat32x3 { vmaxq_f32(x.v, y.v) }; }
	[[nodiscard]] inline vdfloat32x4 max(vdfloat32x4 x, vdfloat32x4 y) { return vdfloat32x4 { vmaxq_f32(x.v, y.v) }; }

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
		int16x4_t v = vqmovn_s32(vcvtnq_s32_f32(x.v));

		return (uint32)vget_lane_u32(
			vreinterpret_u32_u8(
				vqmovun_s16(
					vcombine_s16(v, v)
				)
			),
			0
		) & 0xFFFFFF;
	}

	[[nodiscard]] inline uint32 packus8(vdfloat32x4 x) {
		int16x4_t v = vqmovn_s32(vcvtnq_s32_f32(x.v));

		return (uint32)vget_lane_u32(
			vreinterpret_u32_u8(
				vqmovun_s16(
					vcombine_s16(v, v)
				)
			),
			0
		);
	}

	[[nodiscard]] inline vdint32x4 ceilint(vdfloat32x4 x) {
		return vdint32x4 { vcvtpq_s32_f32(x.v) };
	}
}

using nsVDVecMath::vdmask32x3;
using nsVDVecMath::vdmask32x4;
using nsVDVecMath::vdfloat32x3;
using nsVDVecMath::vdfloat32x4;
using nsVDVecMath::vdint32x4;

#endif
