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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#include <stdafx.h>
#include <at/atcore/decmath.h>

bool ATDecFloat::SetDouble(double v) {
	uint8 bias = 0x40;

	if (v < 0) {
		bias = 0xc0;
		v = -v;
	}

	if (v < 1e-98) {
		SetZero();
		return true;
	}

	static const double invln100 = 0.2171472409516259138255644594583025411471985029018332830572268916;
	double x = floor(log(v) * invln100);
	int ix = (int)x;
	double mantissa = v * pow(100.0, 4-x);

	// compensate for roundoff
	if (mantissa >= 10000000000.0) {
		mantissa /= 100.0;
		++ix;
	} else if (mantissa < 100000000.0) {
		mantissa *= 100.0;
		--ix;
	}

	// convert mantissa to integer (100000000 - 10000000000)
	sint64 imant64 = (sint64)(mantissa + 0.5);

	// renormalize if necessary after rounding
	if (imant64 == 10000000000) {
		imant64 = 100000000;
		++ix;
	}

	// check for underflow
	if (ix < -49) {
		SetZero();
		return true;
	}

	// check for overflow
	if (ix > 49)
		return false;

	// split mantissa into bytes
	uint8 rb[5];

	rb[0] = (uint8)(imant64 / 100000000);
	uint32 imant32 = (uint32)(imant64 % 100000000);

	rb[1] = imant32 / 1000000;
	imant32 %= 1000000;

	rb[2] = imant32 / 10000;
	imant32 %= 10000;

	rb[3] = imant32 / 100;
	imant32 %= 100;

	rb[4] = imant32;

	// convert mantissa to BCD
	for(int i=0; i<5; ++i)
		mMantissa[i] = (uint8)(((rb[i] / 10) << 4) + (rb[i] % 10));

	// encode exponent
	mSignExp = bias + ix;
	return true;
}

ATDecFloat ATDecFloat::operator-() const {
	ATDecFloat r(*this);

	if (r.mSignExp)
		r.mSignExp ^= 0x80;

	return r;
}

ATDecFloat ATDecFloat::Abs() const {
	ATDecFloat r(*this);

	r.mSignExp &= 0x7f;
	return r;
}

ATDecFloatText ATDecFloat::ToString() const {
	ATDecFloatText dt;

	// 18 chars needed: -0.000000000E+100<nul>
	static_assert(std::size(dt.mBuf.ch) >= 18);

	char *dst = dt.mBuf.ch;

	if (!mSignExp || !mMantissa[0])
		*dst++ = '0';
	else {
		int exp = (mSignExp & 0x7f) * 2 - 0x80;

		if (mSignExp & 0x80)
			*dst++ = '-';

		if (mMantissa[0] >= 10) {
			*dst++ = '0' + (mMantissa[0] >> 4);
			*dst++ = '.';
			*dst++ = '0' + (mMantissa[0] & 15);
			++exp;
		} else {
			*dst++ = '0' + (mMantissa[0] & 15);
			*dst++ = '.';
		}

		for(int i=1; i<5; ++i) {
			int v = mMantissa[i];
			*dst++ = '0' + (v >> 4);
			*dst++ = '0' + (v & 15);
		}

		// cut off trailing zeroes
		while(dst[-1] == '0')
			--dst;

		// cut off trailing period
		if (dst[-1] == '.')
			--dst;

		// add exponent
		if (exp) {
			*dst++ = 'E';
			if (exp < 0) {
				*dst++ = '-';
				exp = -exp;
			} else {
				*dst++ = '+';
			}

			// E+100 or higher is not valid in the math pack, but we allow it here
			if (exp >= 100) {
				*dst++ = '1';
				exp -= 100;
			}

			*dst++ = '0' + (exp / 10);
			*dst++ = '0' + (exp % 10);
		}
	}

	*dst = 0;

	static_cast<VDStringSpanA&>(dt) = VDStringSpanA(dt.mBuf.ch, dst);
	return dt;
}

double ATDecFloat::ToDouble() const {
	if (!mSignExp || !mMantissa[0])
		return 0.0;

	int exp = (mSignExp & 0x7f) - 0x40;
	double r = 0;

	for(int i=0; i<5; ++i) {
		int c = mMantissa[i];

		r = (r * 100.0) + (c >> 4)*10 + (c & 15);
	}

	r *= pow(100.0, (double)(exp - 4));

	if (mSignExp & 0x80)
		r = -r;

	return r;
}

bool operator<(const ATDecFloat& x, const ATDecFloat& y) {
	// check for sign difference
	if ((x.mSignExp ^ y.mSignExp) & 0x80)
		return x.mSignExp < 0x80;

	bool xlores = !(x.mSignExp & 0x80);
	bool ylores = !xlores;

	if (x.mSignExp < y.mSignExp)
		return xlores;
	if (x.mSignExp > y.mSignExp)
		return ylores;

	for(int i=0; i<5; ++i) {
		if (x.mMantissa[i] < y.mMantissa[i])
			return xlores;
		if (x.mMantissa[i] > y.mMantissa[i])
			return ylores;
	}

	// values are equal
	return false;
}

bool operator==(const ATDecFloat& x, const ATDecFloat& y) {
	return x.mSignExp == y.mSignExp
		&& x.mMantissa[0] == y.mMantissa[0]
		&& x.mMantissa[1] == y.mMantissa[1]
		&& x.mMantissa[2] == y.mMantissa[2]
		&& x.mMantissa[3] == y.mMantissa[3]
		&& x.mMantissa[4] == y.mMantissa[4];
}

bool operator>(const ATDecFloat& x, const ATDecFloat& y) {
	return y<x;
}

bool operator!=(const ATDecFloat& x, const ATDecFloat& y) {
	return !(x==y);
}

bool operator<=(const ATDecFloat& x, const ATDecFloat& y) {
	return !(x>y);
}

bool operator>=(const ATDecFloat& x, const ATDecFloat& y) {
	return !(x<y);
}

bool ATDecFloatAdd(ATDecFloat& dst, const ATDecFloat& x, const ATDecFloat& y) {
	// Extract exponents
	int xexp = x.mSignExp & 0x7f;
	int yexp = y.mSignExp & 0x7f;

	// Make sure x is larger in magnitude
	if (x.Abs() < y.Abs())
		return ATDecFloatAdd(dst, y, x);

	// Check for y=0.
	if (!y.mSignExp) {
		dst = x;
		return true;
	}

	// Denormalize y.
	ATDecFloat z(y);
	int expdiff = xexp - yexp;
	uint32 round = 0;
	if (expdiff) {
		if (expdiff > 5) {
			dst = x;
			return true;
		}

		// shift 
		while(expdiff--) {
			// keep sticky bit for rounding
			if (round && z.mMantissa[4] == 0x50)
				round = 0x51;
			else
				round = z.mMantissa[4];

			z.mMantissa[4] = z.mMantissa[3];
			z.mMantissa[3] = z.mMantissa[2];
			z.mMantissa[2] = z.mMantissa[1];
			z.mMantissa[1] = z.mMantissa[0];
			z.mMantissa[0] = 0;
		}
	}

	// Set mantissa.
	dst.mSignExp = x.mSignExp;

	// Check if we need to add or subtract.
	if ((x.mSignExp ^ y.mSignExp) & 0x80) {
		// subtract
		uint32 borrow = 0;

		if (round > 0x50 || (round == 0x50 && (z.mMantissa[4] & 0x01)))
			borrow = 1;

		for(int i=4; i>=0; --i) {
			sint32 lo = ((sint32)x.mMantissa[i] & 0x0f) - ((sint32)z.mMantissa[i] & 0x0f) - borrow;
			sint32 hi = ((sint32)x.mMantissa[i] & 0xf0) - ((sint32)z.mMantissa[i] & 0xf0);

			if (lo < 0) {
				lo += 10;
				hi -= 0x10;
			}

			borrow = 0;
			if (hi < 0) {
				hi += 0xA0;
				borrow = 1;
			}

			dst.mMantissa[i] = (uint8)(lo + hi);
		}

		// a borrow out isn't possible
		VDASSERT(!borrow);

		// renormalize as necessary
		for(int i=0; i<5; ++i) {
			if (dst.mMantissa[0])
				break;

			--dst.mSignExp;
			if ((dst.mSignExp & 0x7f) < 64-49) {
				dst.SetZero();
				return true;
			}

			dst.mMantissa[0] = dst.mMantissa[1];
			dst.mMantissa[1] = dst.mMantissa[2];
			dst.mMantissa[2] = dst.mMantissa[3];
			dst.mMantissa[3] = dst.mMantissa[4];
			dst.mMantissa[4] = 0;
		}

		// check for zero
		if (!dst.mMantissa[0])
			dst.mSignExp = 0;
	} else {
		// add
		uint32 carry = 0;

		if (round > 0x50 || (round == 0x50 && (z.mMantissa[4] & 0x01)))
			carry = 1;

		for(int i=4; i>=0; --i) {
			uint32 lo = ((uint32)x.mMantissa[i] & 0x0f) + ((uint32)z.mMantissa[i] & 0x0f) + carry;
			uint32 hi = ((uint32)x.mMantissa[i] & 0xf0) + ((uint32)z.mMantissa[i] & 0xf0);

			if (lo >= 10) {
				lo -= 10;
				hi += 0x10;
			}

			carry = 0;
			if (hi >= 0xA0) {
				hi -= 0xA0;
				carry = 1;
			}

			dst.mMantissa[i] = (uint8)(lo + hi);
		}

		// if we had a carry out, we need to renormalize again
		if (carry) {
			++dst.mSignExp;

			// check for overflow
			if ((dst.mSignExp & 0x7f) > 48+64)
				return false;

			// determine if we need to round up
			uint32 carry2 = 0;
			if (dst.mMantissa[4] > 0x50 || (dst.mMantissa[4] == 0x50 && (dst.mMantissa[3] & 0x01)))
				carry2 = 1;

			// renormalize
			for(int i=3; i>=0; --i) {
				uint32 r = dst.mMantissa[i] + carry2;

				if ((r & 0x0f) >= 0x0A)
					r += 0x06;
				if ((r & 0xf0) >= 0xA0)
					r += 0x60;

				carry2 = r >> 8;
				dst.mMantissa[i+1] = (uint8)r;
			}

			// Unlike base 2 FP, it isn't possible for this to require another renormalize.
			dst.mMantissa[0] = carry + carry2;
		}
	}

	return true;
}

bool ATDecFloatMul(ATDecFloat& dst, const ATDecFloat& x, const ATDecFloat& y) {
	// compute new exponent
	uint8 sign = (x.mSignExp^y.mSignExp) & 0x80;
	sint32 exp = (uint32)(x.mSignExp & 0x7f) + (uint32)(y.mSignExp & 0x7f) - 0x80;

	// convert both mantissae to binary
	int xb[5];
	int yb[5];

	for(int i=0; i<5; ++i) {
		int xm = x.mMantissa[i];
		int ym = y.mMantissa[i];
		xb[i] = ((xm & 0xf0) >> 4)*10 + (xm & 0x0f);
		yb[i] = ((ym & 0xf0) >> 4)*10 + (ym & 0x0f);
	}

	// compute result
	int rb[10] = {0};

	for(int i=0; i<5; ++i) {
		int xbi = xb[i];
		for(int j=0; j<5; ++j)
			rb[i+j] += xbi * yb[j];
	}

	// propagate carries
	int carry = 0;
	for(int i=9; i>=0; --i) {
		rb[i] += carry;
		carry = rb[i] / 100;
		rb[i] %= 100;
	}

	// determine rounding constant
	bool sticky = false;
	if (rb[6] | rb[7] | rb[8] | rb[9])
		sticky = true;

	// shift if necessary
	if (carry) {
		++exp;

		if (rb[5])
			sticky = true;

		rb[5] = rb[4];
		rb[4] = rb[3];
		rb[3] = rb[2];
		rb[2] = rb[1];
		rb[1] = rb[0];
		rb[0] = carry;
	}

	// check if we need to round up
	if (rb[5] > 50 || (rb[5] == 50 && sticky)) {
		if (++rb[4] >= 100) {
			rb[4] = 0;
			if (++rb[3] >= 100) {
				rb[3] = 0;
				if (++rb[2] >= 100) {
					rb[2] = 0;
					if (++rb[1] >= 100) {
						rb[1] = 0;
						if (++rb[0] >= 100) {
							rb[0] = 1;

							++exp;
						}
					}
				}
			}
		}
	}

	// check for underflow
	if (exp < -49) {
		dst.SetZero();
		return true;
	}

	// check for overflow
	if (exp > 49)
		return false;

	// convert digits back to BCD
	for(int i=0; i<5; ++i) {
		int rbi = rb[i];
		dst.mMantissa[i] = (uint8)(((rbi/10) << 4) + (rbi % 10));
	}

	// encode exponent
	dst.mSignExp = (uint8)(sign + exp + 0x40);

	return true;
}

bool ATDecFloatDiv(ATDecFloat& dst, const ATDecFloat& x, const ATDecFloat& y) {
	// check for zero divisor
	if (!y.mSignExp || !y.mMantissa[0])
		return false;

	// check for zero dividend
	if (!x.mSignExp || !x.mMantissa[0]) {
		dst.SetZero();
		return true;
	}

	// compute new exponent
	uint8 sign = (x.mSignExp^y.mSignExp) & 0x80;
	sint32 exp = (uint32)(x.mSignExp & 0x7f) - (uint32)(y.mSignExp & 0x7f);

	// convert both mantissae to binary
	uint64 xb = 0;
	uint64 yb = 0;

	for(int i=0; i<5; ++i) {
		int xm = x.mMantissa[i];
		int ym = y.mMantissa[i];

		xb = (xb * 100) + ((xm & 0xf0) >> 4)*10 + (xm & 0x0f);
		yb = (yb * 100) + ((ym & 0xf0) >> 4)*10 + (ym & 0x0f);
	}

	// do division
	xb *= 10000;
	uint32 v1 = (uint32)(xb / yb);

	xb = (xb % yb) * 1000000;
	uint32 v2 = (uint32)(xb / yb);
	
	bool sticky = (xb % yb) != 0;

	// split digits to base 100
	uint8 rb[6];
	rb[0] = v1 / 10000;		v1 %= 10000;
	rb[1] = v1 / 100;		v1 %= 100;
	rb[2] = v1;
	rb[3] = v2 / 10000;		v2 %= 10000;
	rb[4] = v2 / 100;		v2 %= 100;
	rb[5] = v2;

	// check if we need to renormalize
	if (!rb[0]) {
		rb[0] = rb[1];
		rb[1] = rb[2];
		rb[2] = rb[3];
		rb[3] = rb[4];
		rb[4] = rb[5];
		rb[5] = 0;
		--exp;
	}

	// discard lowest mantissa byte and update rounder
	int rounder = (rb[5] - 50);
	if (!rounder && sticky)
		rounder = 1;
	
	// check if we need to round up
	if (rounder > 0 || (rounder == 0 && sticky)) {
		if (++rb[4] >= 100) {
			rb[4] = 0;
			if (++rb[3] >= 100) {
				rb[3] = 0;
				if (++rb[2] >= 100) {
					rb[2] = 0;
					if (++rb[1] >= 100) {
						rb[1] = 0;
						if (++rb[0] >= 100) {
							rb[0] = 1;
							++exp;
						}
					}
				}
			}
		}
	}

	// convert digits back to BCD
	for(int i=0; i<5; ++i) {
		int rbi = rb[i];
		dst.mMantissa[i] = (uint8)(((rbi/10) << 4) + (rbi % 10));
	}

	// check for underflow or overflow
	if (exp < -49) {
		dst.SetZero();
		return true;
	}

	if (exp > 49)
		return false;

	// encode exponent
	dst.mSignExp = (uint8)(sign + exp + 0x40);

	return true;
}
