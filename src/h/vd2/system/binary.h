//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
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

#ifndef f_VD2_SYSTEM_BINARY_H
#define f_VD2_SYSTEM_BINARY_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vdtypes.h>

#define VDMAKEFOURCC(byte1, byte2, byte3, byte4) (((uint8)byte1) + (((uint8)byte2) << 8) + (((uint8)byte3) << 16) + (((uint8)byte4) << 24))

#ifdef _MSC_VER
	#include <vd2/system/win32/intrin.h>

	inline constexpr uint16 VDSwizzleU16(uint16 value) {
		if (std::is_constant_evaluated()) {
			return (value >> 8) + (value << 8);
		} else {
			return (uint16)_byteswap_ushort((unsigned short)value);
		}
	}
	
	inline constexpr sint16 VDSwizzleS16(sint16 value) {
		if (std::is_constant_evaluated()) {
			return (sint16)(((uint16)value >> 8) + ((uint16)value << 8));
		} else {
			return (sint16)_byteswap_ushort((unsigned short)value);
		}
	}

	inline constexpr uint32 VDSwizzleU32(uint32 value) {
		if (std::is_constant_evaluated()) {
			return (value >> 24) + (value << 24) + ((value&0xff00)<<8) + ((value&0xff0000)>>8);
		} else {
			return (uint32)_byteswap_ulong((unsigned long)value);
		}
	}

	inline constexpr sint32 VDSwizzleS32(sint32 value) {
		if (std::is_constant_evaluated()) {
			return (sint32)(((uint32)value >> 24) + ((uint32)value << 24) + (((uint32)value&0xff00)<<8) + (((uint32)value&0xff0000)>>8));
		} else {
			return (sint32)_byteswap_ulong((unsigned long)value);
		}
	}

	inline constexpr uint64 VDSwizzleU64(uint64 value) {
		if (std::is_constant_evaluated()) {
			return	((value & 0xFF00000000000000) >> 56) +
					((value & 0x00FF000000000000) >> 40) +
					((value & 0x0000FF0000000000) >> 24) +
					((value & 0x000000FF00000000) >>  8) +
					((value & 0x00000000FF000000) <<  8) +
					((value & 0x0000000000FF0000) << 24) +
					((value & 0x000000000000FF00) << 40) +
					((value & 0x00000000000000FF) << 56);
		} else {
			return (uint64)_byteswap_uint64((unsigned __int64)value);
		}
	}

	inline constexpr sint64 VDSwizzleS64(sint64 value) {
		if (std::is_constant_evaluated()) {
			return (sint64)((((uint64)value & 0xFF00000000000000) >> 56) +
							(((uint64)value & 0x00FF000000000000) >> 40) +
							(((uint64)value & 0x0000FF0000000000) >> 24) +
							(((uint64)value & 0x000000FF00000000) >>  8) +
							(((uint64)value & 0x00000000FF000000) <<  8) +
							(((uint64)value & 0x0000000000FF0000) << 24) +
							(((uint64)value & 0x000000000000FF00) << 40) +
							(((uint64)value & 0x00000000000000FF) << 56));
		} else {
			return (sint64)_byteswap_uint64((unsigned __int64)value);
		}
	}

	inline uint32 VDRotateLeftU32(uint32 value, int bits) { return (uint32)_rotl((unsigned int)value, bits); }
	inline uint32 VDRotateRightU32(uint32 value, int bits) { return (uint32)_rotr((unsigned int)value, bits); }
#else
	inline constexpr uint16 VDSwizzleU16(uint16 value) {
		return (value >> 8) + (value << 8);
	}

	inline constexpr sint16 VDSwizzleS16(sint16 value) {
		return (sint16)(((uint16)value >> 8) + ((uint16)value << 8));
	}

	inline constexpr uint32 VDSwizzleU32(uint32 value) {
		return (value >> 24) + (value << 24) + ((value&0xff00)<<8) + ((value&0xff0000)>>8);
	}

	inline constexpr sint32 VDSwizzleS32(sint32 value) {
		return (sint32)(((uint32)value >> 24) + ((uint32)value << 24) + (((uint32)value&0xff00)<<8) + (((uint32)value&0xff0000)>>8));
	}

	inline constexpr uint64 VDSwizzleU64(uint64 value) {
		return	((value & 0xFF00000000000000) >> 56) +
				((value & 0x00FF000000000000) >> 40) +
				((value & 0x0000FF0000000000) >> 24) +
				((value & 0x000000FF00000000) >>  8) +
				((value & 0x00000000FF000000) <<  8) +
				((value & 0x0000000000FF0000) << 24) +
				((value & 0x000000000000FF00) << 40) +
				((value & 0x00000000000000FF) << 56);
	}

	inline constexpr sint64 VDSwizzleS64(sint64 value) {
		return (sint64)((((uint64)value & 0xFF00000000000000) >> 56) +
						(((uint64)value & 0x00FF000000000000) >> 40) +
						(((uint64)value & 0x0000FF0000000000) >> 24) +
						(((uint64)value & 0x000000FF00000000) >>  8) +
						(((uint64)value & 0x00000000FF000000) <<  8) +
						(((uint64)value & 0x0000000000FF0000) << 24) +
						(((uint64)value & 0x000000000000FF00) << 40) +
						(((uint64)value & 0x00000000000000FF) << 56));
	}
#endif

#if defined(__clang__) || defined(__GNUC__)
template<typename T>
struct VDUnalignedHelper {
	T v __attribute__((packed));
};

inline uint16 VDReadUnalignedU16(const void *p) { return ((VDUnalignedHelper<uint16> __attribute__((may_alias)) *)p)->v; }
inline sint16 VDReadUnalignedS16(const void *p) { return ((VDUnalignedHelper<sint16> __attribute__((may_alias)) *)p)->v; }
inline uint32 VDReadUnalignedU32(const void *p) { return ((VDUnalignedHelper<uint32> __attribute__((may_alias)) *)p)->v; }
inline sint32 VDReadUnalignedS32(const void *p) { return ((VDUnalignedHelper<sint32> __attribute__((may_alias)) *)p)->v; }
inline uint64 VDReadUnalignedU64(const void *p) { return ((VDUnalignedHelper<uint64> __attribute__((may_alias)) *)p)->v; }
inline sint64 VDReadUnalignedS64(const void *p) { return ((VDUnalignedHelper<sint64> __attribute__((may_alias)) *)p)->v; }
inline float  VDReadUnalignedF  (const void *p) { return ((VDUnalignedHelper<float > __attribute__((may_alias)) *)p)->v; }
inline double VDReadUnalignedD  (const void *p) { return ((VDUnalignedHelper<double> __attribute__((may_alias)) *)p)->v; }

inline uint16 VDReadUnalignedLEU16(const void *p) { return ((VDUnalignedHelper<uint16> __attribute__((may_alias)) *)p)->v; }
inline sint16 VDReadUnalignedLES16(const void *p) { return ((VDUnalignedHelper<sint16> __attribute__((may_alias)) *)p)->v; }
inline uint32 VDReadUnalignedLEU32(const void *p) { return ((VDUnalignedHelper<uint32> __attribute__((may_alias)) *)p)->v; }
inline sint32 VDReadUnalignedLES32(const void *p) { return ((VDUnalignedHelper<sint32> __attribute__((may_alias)) *)p)->v; }
inline uint64 VDReadUnalignedLEU64(const void *p) { return ((VDUnalignedHelper<uint64> __attribute__((may_alias)) *)p)->v; }
inline sint64 VDReadUnalignedLES64(const void *p) { return ((VDUnalignedHelper<sint64> __attribute__((may_alias)) *)p)->v; }
inline float  VDReadUnalignedLEF  (const void *p) { return ((VDUnalignedHelper<float > __attribute__((may_alias)) *)p)->v; }
inline double VDReadUnalignedLED  (const void *p) { return ((VDUnalignedHelper<double> __attribute__((may_alias)) *)p)->v; }

inline uint16 VDReadUnalignedBEU16(const void *p) { return VDSwizzleU16(((VDUnalignedHelper<uint16> __attribute__((may_alias)) *)p)->v); }
inline sint16 VDReadUnalignedBES16(const void *p) { return VDSwizzleS16(((VDUnalignedHelper<sint16> __attribute__((may_alias)) *)p)->v); }
inline uint32 VDReadUnalignedBEU32(const void *p) { return VDSwizzleU32(((VDUnalignedHelper<uint32> __attribute__((may_alias)) *)p)->v); }
inline sint32 VDReadUnalignedBES32(const void *p) { return VDSwizzleS32(((VDUnalignedHelper<sint32> __attribute__((may_alias)) *)p)->v); }
inline uint64 VDReadUnalignedBEU64(const void *p) { return VDSwizzleU64(((VDUnalignedHelper<uint64> __attribute__((may_alias)) *)p)->v); }
inline sint64 VDReadUnalignedBES64(const void *p) { return VDSwizzleS64(((VDUnalignedHelper<sint64> __attribute__((may_alias)) *)p)->v); }
inline float VDReadUnalignedBEF(const void *p) {
	union {
		uint32 i;
		float f;
	} conv = {VDSwizzleU32(((VDUnalignedHelper<uint32> __attribute__((may_alias)) *)p)->v)};
	return conv.f;
}
inline double VDReadUnalignedBED(const void *p) {
	union {
		uint64 i;
		double d;
	} conv = {VDSwizzleU64(((VDUnalignedHelper<uint64> __attribute__((may_alias)) *)p)->v)};
	return conv.d;
}

inline void VDWriteUnalignedU16  (void *p, uint16 v) { ((VDUnalignedHelper<uint16> __attribute__((may_alias)) *)p)->v = v; }
inline void VDWriteUnalignedS16  (void *p, sint16 v) { ((VDUnalignedHelper<sint16> __attribute__((may_alias)) *)p)->v = v; }
inline void VDWriteUnalignedU32  (void *p, uint32 v) { ((VDUnalignedHelper<uint32> __attribute__((may_alias)) *)p)->v = v; }
inline void VDWriteUnalignedS32  (void *p, sint32 v) { ((VDUnalignedHelper<sint32> __attribute__((may_alias)) *)p)->v = v; }
inline void VDWriteUnalignedU64  (void *p, uint64 v) { ((VDUnalignedHelper<uint64> __attribute__((may_alias)) *)p)->v = v; }
inline void VDWriteUnalignedS64  (void *p, sint64 v) { ((VDUnalignedHelper<sint64> __attribute__((may_alias)) *)p)->v = v; }
inline void VDWriteUnalignedF    (void *p, float  v) { ((VDUnalignedHelper<float > __attribute__((may_alias)) *)p)->v = v; }
inline void VDWriteUnalignedD    (void *p, double v) { ((VDUnalignedHelper<double> __attribute__((may_alias)) *)p)->v = v; }

inline void VDWriteUnalignedLEU16(void *p, uint16 v) { ((VDUnalignedHelper<uint16> __attribute__((may_alias)) *)p)->v = v; }
inline void VDWriteUnalignedLES16(void *p, sint16 v) { ((VDUnalignedHelper<sint16> __attribute__((may_alias)) *)p)->v = v; }
inline void VDWriteUnalignedLEU32(void *p, uint32 v) { ((VDUnalignedHelper<uint32> __attribute__((may_alias)) *)p)->v = v; }
inline void VDWriteUnalignedLES32(void *p, sint32 v) { ((VDUnalignedHelper<sint32> __attribute__((may_alias)) *)p)->v = v; }
inline void VDWriteUnalignedLEU64(void *p, uint64 v) { ((VDUnalignedHelper<uint64> __attribute__((may_alias)) *)p)->v = v; }
inline void VDWriteUnalignedLES64(void *p, sint64 v) { ((VDUnalignedHelper<sint64> __attribute__((may_alias)) *)p)->v = v; }
inline void VDWriteUnalignedLEF  (void *p, float  v) { ((VDUnalignedHelper<float > __attribute__((may_alias)) *)p)->v = v; }
inline void VDWriteUnalignedLED  (void *p, double v) { ((VDUnalignedHelper<double> __attribute__((may_alias)) *)p)->v = v; }

inline void VDWriteUnalignedBEU16(void *p, uint16 v) { ((VDUnalignedHelper<uint16> __attribute__((may_alias)) *)p)->v = VDSwizzleU16(v); }
inline void VDWriteUnalignedBES16(void *p, sint16 v) { ((VDUnalignedHelper<sint16> __attribute__((may_alias)) *)p)->v = VDSwizzleS16(v); }
inline void VDWriteUnalignedBEU32(void *p, uint32 v) { ((VDUnalignedHelper<uint32> __attribute__((may_alias)) *)p)->v = VDSwizzleU32(v); }
inline void VDWriteUnalignedBES32(void *p, sint32 v) { ((VDUnalignedHelper<sint32> __attribute__((may_alias)) *)p)->v = VDSwizzleS32(v); }
inline void VDWriteUnalignedBEU64(void *p, uint64 v) { ((VDUnalignedHelper<uint64> __attribute__((may_alias)) *)p)->v = VDSwizzleU64(v); }
inline void VDWriteUnalignedBES64(void *p, sint64 v) { ((VDUnalignedHelper<sint64> __attribute__((may_alias)) *)p)->v = VDSwizzleS64(v); }
inline void VDWriteUnalignedBEF(void *p, float v) {
	union {
		float f;
		uint32 i;
	} conv = {v};
	((VDUnalignedHelper<uint32> __attribute__((may_alias)) *)p)->v = VDSwizzleU32(conv.i);
}
inline void VDWriteUnalignedBED(void *p, double v) {
	union {
		double f;
		uint64 i;
	} conv = {v};
	((VDUnalignedHelper<uint64> __attribute__((may_alias)) *)p)->v = VDSwizzleU64(conv.i);
}
#else
inline uint16 VDReadUnalignedU16(const void *p) { return *(uint16 *)p; }
inline sint16 VDReadUnalignedS16(const void *p) { return *(sint16 *)p; }
inline uint32 VDReadUnalignedU32(const void *p) { return *(uint32 *)p; }
inline sint32 VDReadUnalignedS32(const void *p) { return *(sint32 *)p; }
inline uint64 VDReadUnalignedU64(const void *p) { return *(uint64 *)p; }
inline sint64 VDReadUnalignedS64(const void *p) { return *(sint64 *)p; }
inline float VDReadUnalignedF(const void *p) { return *(float *)p; }
inline double VDReadUnalignedD(const void *p) { return *(double *)p; }

inline uint16 VDReadUnalignedLEU16(const void *p) { return *(uint16 *)p; }
inline sint16 VDReadUnalignedLES16(const void *p) { return *(sint16 *)p; }
inline uint32 VDReadUnalignedLEU32(const void *p) { return *(uint32 *)p; }
inline sint32 VDReadUnalignedLES32(const void *p) { return *(sint32 *)p; }
inline uint64 VDReadUnalignedLEU64(const void *p) { return *(uint64 *)p; }
inline sint64 VDReadUnalignedLES64(const void *p) { return *(sint64 *)p; }
inline float VDReadUnalignedLEF(const void *p) { return *(float *)p; }
inline double VDReadUnalignedLED(const void *p) { return *(double *)p; }

inline uint16 VDReadUnalignedBEU16(const void *p) { return VDSwizzleU16(*(uint16 *)p); }
inline sint16 VDReadUnalignedBES16(const void *p) { return VDSwizzleS16(*(sint16 *)p); }
inline uint32 VDReadUnalignedBEU32(const void *p) { return VDSwizzleU32(*(uint32 *)p); }
inline sint32 VDReadUnalignedBES32(const void *p) { return VDSwizzleS32(*(sint32 *)p); }
inline uint64 VDReadUnalignedBEU64(const void *p) { return VDSwizzleU64(*(uint64 *)p); }
inline sint64 VDReadUnalignedBES64(const void *p) { return VDSwizzleS64(*(sint64 *)p); }
inline float VDReadUnalignedBEF(const void *p) {
	union {
		uint32 i;
		float f;
	} conv = {VDSwizzleU32(*(const uint32 *)p)};
	return conv.f;
}
inline double VDReadUnalignedBED(const void *p) {
	union {
		uint64 i;
		double d;
	} conv = {VDSwizzleU64(*(const uint32 *)p)};
	return conv.d;
}

inline void VDWriteUnalignedU16  (void *p, uint16 v) { *(uint16 *)p = v; }
inline void VDWriteUnalignedS16  (void *p, sint16 v) { *(sint16 *)p = v; }
inline void VDWriteUnalignedU32  (void *p, uint32 v) { *(uint32 *)p = v; }
inline void VDWriteUnalignedS32  (void *p, sint32 v) { *(sint32 *)p = v; }
inline void VDWriteUnalignedU64  (void *p, uint64 v) { *(uint64 *)p = v; }
inline void VDWriteUnalignedS64  (void *p, sint64 v) { *(sint64 *)p = v; }
inline void VDWriteUnalignedF    (void *p, float  v) { *(float  *)p = v; }
inline void VDWriteUnalignedD    (void *p, double v) { *(double *)p = v; }

inline void VDWriteUnalignedLEU16(void *p, uint16 v) { *(uint16 *)p = v; }
inline void VDWriteUnalignedLES16(void *p, sint16 v) { *(sint16 *)p = v; }
inline void VDWriteUnalignedLEU32(void *p, uint32 v) { *(uint32 *)p = v; }
inline void VDWriteUnalignedLES32(void *p, sint32 v) { *(sint32 *)p = v; }
inline void VDWriteUnalignedLEU64(void *p, uint64 v) { *(uint64 *)p = v; }
inline void VDWriteUnalignedLES64(void *p, sint64 v) { *(sint64 *)p = v; }
inline void VDWriteUnalignedLEF  (void *p, float  v) { *(float  *)p = v; }
inline void VDWriteUnalignedLED  (void *p, double v) { *(double *)p = v; }

inline void VDWriteUnalignedBEU16(void *p, uint16 v) { *(uint16 *)p = VDSwizzleU16(v); }
inline void VDWriteUnalignedBES16(void *p, sint16 v) { *(sint16 *)p = VDSwizzleS16(v); }
inline void VDWriteUnalignedBEU32(void *p, uint32 v) { *(uint32 *)p = VDSwizzleU32(v); }
inline void VDWriteUnalignedBES32(void *p, sint32 v) { *(sint32 *)p = VDSwizzleS32(v); }
inline void VDWriteUnalignedBEU64(void *p, uint64 v) { *(uint64 *)p = VDSwizzleU64(v); }
inline void VDWriteUnalignedBES64(void *p, sint64 v) { *(sint64 *)p = VDSwizzleS64(v); }
inline void VDWriteUnalignedBEF(void *p, float v) {
	union {
		float f;
		uint32 i;
	} conv = {v};
	*(uint32 *)p = VDSwizzleU32(conv.i);
}
inline void VDWriteUnalignedBED(void *p, double v) {
	union {
		double f;
		uint64 i;
	} conv = {v};
	*(uint64 *)p = VDSwizzleU64(conv.i);
}
#endif

#define VDFromLE8(x)	(x)
#define VDFromLE16(x)	(x)
#define VDFromLE32(x)	(x)
#define VDFromLE64(x)	(x)
#define VDFromBE8(x)	VDSwizzleU8(x)
#define VDFromBE16(x)	VDSwizzleU16(x)
#define VDFromBE32(x)	VDSwizzleU32(x)
#define VDFromBE64(x)	VDSwizzleU64(x)
#define VDFromBEU8(x)	VDSwizzleU8(x)
#define VDFromBEU16(x)	VDSwizzleU16(x)
#define VDFromBEU32(x)	VDSwizzleU32(x)
#define VDFromBEU64(x)	VDSwizzleU64(x)

#define VDToLE8(x)		(x)
#define VDToLE16(x)		(x)
#define VDToLE32(x)		(x)
#define VDToLE64(x)		(x)
#define VDToBE8(x)		VDSwizzleU8(x)
#define VDToBE16(x)		VDSwizzleU16(x)
#define VDToBE32(x)		VDSwizzleU32(x)
#define VDToBE64(x)		VDSwizzleU64(x)
#define VDToBEU8(x)		VDSwizzleU8(x)
#define VDToBEU16(x)	VDSwizzleU16(x)
#define VDToBEU32(x)	VDSwizzleU32(x)
#define VDToBEU64(x)	VDSwizzleU64(x)

////////////////////////////////////////////////////////////////////////////////

template<typename T>
struct vdbe;

template<> struct vdbe<uint8> { uint8 val; vdbe() = default; vdbe(uint8 v) : val(v) {} vdbe& operator=(uint8 v) { val = v; return *this; } operator uint8() const { return val; } };
template<> struct vdbe<sint8> { sint8 val; vdbe() = default; vdbe(sint8 v) : val(v) {} vdbe& operator=(sint8 v) { val = v; return *this; } operator sint8() const { return val; } };
template<> struct vdbe<uint16> { uint8 buf[2]; vdbe() = default; vdbe(uint16 v) { VDWriteUnalignedBEU16(buf, v); } vdbe& operator=(uint16 v) { VDWriteUnalignedBEU16(buf, v); return *this; } operator uint16() const { return VDReadUnalignedBEU16(buf); } };
template<> struct vdbe<sint16> { uint8 buf[2]; vdbe() = default; vdbe(sint16 v) { VDWriteUnalignedBEU16(buf, v); } vdbe& operator=(sint16 v) { VDWriteUnalignedBEU16(buf, v); return *this; } operator sint16() const { return VDReadUnalignedBEU16(buf); } };
template<> struct vdbe<uint32> { uint8 buf[4]; vdbe() = default; vdbe(uint32 v) { VDWriteUnalignedBEU32(buf, v); } vdbe& operator=(uint32 v) { VDWriteUnalignedBEU32(buf, v); return *this; } operator uint32() const { return VDReadUnalignedBEU32(buf); } };
template<> struct vdbe<sint32> { uint8 buf[4]; vdbe() = default; vdbe(sint32 v) { VDWriteUnalignedBEU32(buf, v); } vdbe& operator=(sint32 v) { VDWriteUnalignedBEU32(buf, v); return *this; } operator sint32() const { return VDReadUnalignedBEU32(buf); } };
template<> struct vdbe<uint64> { uint8 buf[8]; vdbe() = default; vdbe(uint64 v) { VDWriteUnalignedBEU64(buf, v); } vdbe& operator=(uint64 v) { VDWriteUnalignedBEU64(buf, v); return *this; } operator uint64() const { return VDReadUnalignedBEU64(buf); } };
template<> struct vdbe<sint64> { uint8 buf[8]; vdbe() = default; vdbe(sint64 v) { VDWriteUnalignedBEU64(buf, v); } vdbe& operator=(sint64 v) { VDWriteUnalignedBEU64(buf, v); return *this; } operator sint64() const { return VDReadUnalignedBEU64(buf); } };

#endif
