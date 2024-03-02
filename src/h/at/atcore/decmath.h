//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2019 Avery Lee
//	Decimal math support
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_ATCORE_DECMATH_H
#define f_AT_ATCORE_DECMATH_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>

struct ATDecFloatText final : public VDStringSpanA {
	ATDecFloatText() = default;

	vdnothrow ATDecFloatText(const ATDecFloatText& src) vdnoexcept
		: VDStringSpanA(mBuf.ch, mBuf.ch + src.size())
		, mBuf(src.mBuf)
	{
	}

	vdnothrow ATDecFloatText& operator=(const ATDecFloatText& src) vdnoexcept {
		if (&src != this) {
			mBuf = src.mBuf;
			static_cast<VDStringSpanA&>(*this) = VDStringSpanA(mBuf.ch, mBuf.ch + src.size());
		}

		return *this;
	}

	const char *c_str() const { return mBuf.ch; }

	struct Buffer {
		char ch[18];
	} mBuf;
};

struct ATDecFloat {
	uint8	mSignExp;
	uint8	mMantissa[5];

	static ATDecFloat FromBytes(const void *fp6) {
		ATDecFloat x;
		memcpy(&x, fp6, 6);

		return x;
	}

	static ATDecFloat FromDouble(double x) {
		ATDecFloat d;
		d.SetDouble(x);
		return d;
	}

	static constexpr ATDecFloat Zero() {
		constexpr ATDecFloat zero { 0x00, { 0x00, 0x00, 0x00, 0x00, 0x00 } };
		return zero;
	}

	static constexpr ATDecFloat One() {
		constexpr ATDecFloat one { 0x40, { 0x01, 0x00, 0x00, 0x00, 0x00 } };
		return one;
	}

	void SetZero() { *this = Zero(); }
	void SetOne() { *this = One(); }

	bool SetDouble(double d);

	ATDecFloat operator-() const;

	ATDecFloat Abs() const;

	ATDecFloatText ToString() const;
	double ToDouble() const;
};

bool operator< (const ATDecFloat& x, const ATDecFloat& y);
bool operator==(const ATDecFloat& x, const ATDecFloat& y);
bool operator> (const ATDecFloat& x, const ATDecFloat& y);
bool operator!=(const ATDecFloat& x, const ATDecFloat& y);
bool operator<=(const ATDecFloat& x, const ATDecFloat& y);
bool operator>=(const ATDecFloat& x, const ATDecFloat& y);

bool ATDecFloatAdd(ATDecFloat& dst, const ATDecFloat& x, const ATDecFloat& y);
bool ATDecFloatMul(ATDecFloat& dst, const ATDecFloat& x, const ATDecFloat& y);
bool ATDecFloatDiv(ATDecFloat& dst, const ATDecFloat& x, const ATDecFloat& y);

#endif
