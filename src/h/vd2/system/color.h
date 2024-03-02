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

#ifndef f_VD2_SYSTEM_COLOR_H
#define f_VD2_SYSTEM_COLOR_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/vecmath.h>

template<typename T>
class VDPackedColor888 {
public:
	VDPackedColor888()
		: mPackedColor(0)
	{
	}

	explicit VDPackedColor888(uint32 v)
		: mPackedColor(v)
	{
	}

	explicit operator uint32() const { return mPackedColor; }

	T Half() const {
		return T((mPackedColor & 0xfefefe) >> 1);
	}

	T Quarter() const {
		return T((mPackedColor & 0xfcfcfc) >> 2);
	}

	static T Average(const T& x, const T& y) {
		return T((x.mPackedColor | y.mPackedColor) - (((x.mPackedColor ^ y.mPackedColor) & 0xfefefe) >> 1));
	}

protected:
	uint32 mPackedColor;
};

class VDPackedColorRGB8 : public VDPackedColor888<VDPackedColorRGB8> {
public:
	using VDPackedColor888::VDPackedColor888;

	VDPackedColorRGB8(uint8 r, uint8 g, uint8 b)
		: VDPackedColor888(((uint32)r << 16) + ((uint32)g << 8) + b)
	{
	}
};

class VDPackedColorBGR8 : public VDPackedColor888<VDPackedColorBGR8> {
public:
	using VDPackedColor888::VDPackedColor888;

	VDPackedColorBGR8(uint8 r, uint8 g, uint8 b)
		: VDPackedColor888(((uint32)b << 16) + ((uint32)g << 8) + r)
	{
	}
};

class VDColorRGB {
public:
	explicit VDColorRGB(VDPackedColorRGB8 c)
		: v(vdfloat32x4::unpacku8((uint32)c))
	{
	}

	explicit VDColorRGB(VDPackedColorBGR8 c)
		: v(nsVDVecMath::permute<2,1,0,3>(vdfloat32x4::unpacku8((uint32)c)))
	{
	}

	explicit VDColorRGB(vdfloat32x4 c)
		: v(c)
	{
	}

	explicit operator vdfloat32x4() const { return v; }

	static VDColorRGB FromBGR8(uint32 c) {
		return VDColorRGB(vdfloat32x4::unpacku8((uint32)c) * (1.0f / 255.0f));
	}

	static VDColorRGB FromRGB8(uint32 c) {
		return VDColorRGB(nsVDVecMath::permute<2,1,0,3>(vdfloat32x4::unpacku8((uint32)c)) * (1.0f / 255.0f));
	}

	uint32 ToBGR8() const {
		return packus8(v * 255.0f) & 0xFFFFFF;
	}

	uint32 ToRGB8() const {
		return packus8(nsVDVecMath::permute<2,1,0,3>(v) * 255.0f) & 0xFFFFFF;
	}

	VDColorRGB SRGBToLinear() const {
		vdfloat32x4 x = (v + 0.055f) * (1.0f / 1.055f);
		vdfloat32x4 y = pow(x, 2.4f);

		return VDColorRGB(select(v < vdfloat32x4::set1(0.04045f), v * (1.0f / 12.92f), y));
	}

	VDColorRGB LinearToSRGB() const {
		vdfloat32x4 y = 1.055f * pow(v, 1.0f / 2.4f) - 0.055f;

		return VDColorRGB(select(v < vdfloat32x4::set1(0.0031308f), v * 12.92f, y));
	}

	float Luma() const {
		vdfloat32x4 luma = v * vdfloat32x4::set(0.2126f, 0.7152f, 0.0722f, 0.0f);
		return luma.x() + luma.y() + luma.z();
	}

	VDColorRGB operator*(float s) { 
		return VDColorRGB(v*s);
	}

	VDColorRGB operator+(float s) const {
		return VDColorRGB(v + s);
	}

	VDColorRGB operator-(float s) const {
		return VDColorRGB(v - s);
	}

	VDColorRGB operator-(VDColorRGB y) const {
		return VDColorRGB(v - y.v);
	}

	VDColorRGB operator+(VDColorRGB y) const {
		return VDColorRGB(v + y.v);
	}

private:
	vdfloat32x4 v;
};

#endif
