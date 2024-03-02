//	Altirra - Atari 800/800XL/5200 emulator
//	I/O library - cassette FSK and direct decoders
//	Copyright (C) 2009-2016 Avery Lee
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

#ifndef f_AT_ATIO_CASSETTEDECODER_H
#define f_AT_ATIO_CASSETTEDECODER_H

class ATCassetteDecoderFSK {
public:
	ATCassetteDecoderFSK();

	void Reset();
	bool Advance(float x);

protected:
	float mHistory[64];
	float mHistorySum3[64];
	float mHistorySum4[64];
	int mIndex;
};

ATCassetteDecoderFSK::ATCassetteDecoderFSK() {
	Reset();
}

void ATCassetteDecoderFSK::Reset() {
	mIndex = 0;
	memset(mHistory, 0, sizeof mHistory);
	memset(mHistorySum3, 0, sizeof mHistorySum3);
	memset(mHistorySum4, 0, sizeof mHistorySum4);
}

bool ATCassetteDecoderFSK::Advance(float x) {
	int base = mIndex++ & 31;
	int write = (base + 31) & 31;

	mHistory[write] = mHistory[write + 32] = x;

	// 3995Hz filter has an 8 cycle period.
	// 5327Hz filter has a 6 cycle period.
	//
	// We select the appropriate bins from a 24-point DFT and compute the
	// magnitude for each vector.

	const float *xs = &mHistory[base + 8];
	mHistorySum3[write] = mHistorySum3[write + 32] = xs[7] + xs[15] + xs[23];
	mHistorySum4[write] = mHistorySum4[write + 32] = xs[5] + xs[11] + xs[17] + xs[23];

	const float *s3 = &mHistorySum3[base + 32 - 8];
	const float *s4 = &mHistorySum4[base + 32 - 6];

	const float x0 = s3[0];
	const float x1 = s3[1];
	const float x2 = s3[2];
	const float x3 = s3[3];
	const float x4 = s3[4];
	const float x5 = s3[5];
	const float x6 = s3[6];
	const float x7 = s3[7];

	const float sqrt2 = 0.707107f;
	float x1mx5 = x1 - x5;
	float x3mx7 = x3 - x7;
	float zero_s = (x2 - x6) + (x1mx5 + x3mx7)*sqrt2;
	float zero_c = (x0 - x4) + (x1mx5 - x3mx7)*sqrt2;

	const float y1 = s4[0];
	const float y2 = s4[1];
	const float y3 = s4[2];
	const float y4 = s4[3];
	const float y5 = s4[4];
	const float y6 = s4[5];

	const float half = 0.5f;
	const float sin2_12 = 0.86602540378443864676372317075294f;

	float y2my5 = y2 - y5;
	float y3my6 = y3 - y6;
	float one_s = (y2my5 + y3my6)*sin2_12;
	float one_c = (y1 - y4) + (y2my5 - y3my6)*half;

	float one = (one_s * one_s + one_c * one_c) * ((8.0f / 6.0f) * (8.0f / 6.0f));
	float zero = (zero_s * zero_s + zero_c * zero_c);

	return one > zero;
}

///////////////////////////////////////////////////////////////////////////////

class ATCassetteDecoderDirect {
public:
	ATCassetteDecoderDirect();

	void Reset();
	bool Advance(float x);

protected:
	float mZeroLevel;
	float mOneLevel;
	bool mbCurrentState;
};

ATCassetteDecoderDirect::ATCassetteDecoderDirect() {
	Reset();
}

void ATCassetteDecoderDirect::Reset() {
	mZeroLevel = 0.0f;
	mOneLevel = 1.0f;
}

bool ATCassetteDecoderDirect::Advance(float x) {
	float range = mOneLevel - mZeroLevel;
	float oneThird = mZeroLevel + range * (1.0f / 3.0f);
	float twoThirds = mZeroLevel + range * (2.0f / 3.0f);

	if (x < oneThird) {
		mbCurrentState = false;

		mZeroLevel += (x - mZeroLevel) * 0.95f;
	} else if (x > twoThirds) {
		mbCurrentState = true;

		mOneLevel += (x - mOneLevel) * 0.95f;
	}

	return mbCurrentState;
}

#endif
