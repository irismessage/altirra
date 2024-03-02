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
	inline ATCassetteDecoderFSK();

	inline void Reset();
	inline bool Advance(float x);

protected:
	float mAcc0R;
	float mAcc0I;
	float mAcc1R;
	float mAcc1I;
	uint32 mIndex;
	float mHistory[64];
};

ATCassetteDecoderFSK::ATCassetteDecoderFSK() {
	Reset();
}

void ATCassetteDecoderFSK::Reset() {
	mAcc0R = 0;
	mAcc0I = 0;
	mAcc1R = 0;
	mAcc1I = 0;
	mIndex = 0;
	memset(mHistory, 0, sizeof mHistory);
}

bool ATCassetteDecoderFSK::Advance(float x) {
	uint32 hpos1 = mIndex & 31;

	++mIndex;

	// update history window
	mHistory[hpos1] = mHistory[hpos1 + 32] = x;

	// We sample at 31960Hz and use a 24-point DFT.
	// 3995Hz (zero) filter extracts from bin 3.
	// 5327Hz (one) filter extracts from bin 4.
	//
	// We compute these via a sliding DFT. The per-sample phase shift angles
	// for the two filters are 2*pi/24*3 = pi/4 and 2*pi/24*4 = pi/3. On top
	// of that, We also need a tiny 'r' constant anyway to prevent
	// accumulation error from blowing up the filter.
	//
	// This filter introduces a delay of 12 samples; currently we just ignore
	// that.

	constexpr float kR = 0.999997f;
	constexpr float kPhaseShift0 = 0.70710678118654752440084436210485f * kR;
	constexpr float kPhaseShift1R = 0.5f * kR;
	constexpr float kPhaseShift1I = 0.86602540378443864676372317075294f * kR;

	const float y = x - mHistory[hpos1 + 32 - 24];

	const float acc0r = (mAcc0R - mAcc0I) * kPhaseShift0 + y;
	const float acc0i = (mAcc0R + mAcc0I) * kPhaseShift0;
	mAcc0R = acc0r;
	mAcc0I = acc0i;

	const float acc1r = mAcc1R * kPhaseShift1R - mAcc1I * kPhaseShift1I + y;
	const float acc1i = mAcc1R * kPhaseShift1I + mAcc1I * kPhaseShift1R;
	mAcc1R = acc1r;
	mAcc1I = acc1i;
		
	const float zero = acc0r * acc0r + acc0i * acc0i;
	const float one = acc1r * acc1r + acc1i * acc1i;

	return one >= zero;
}

///////////////////////////////////////////////////////////////////////////////

class ATCassetteDecoderDirect {
public:
	inline ATCassetteDecoderDirect();

	inline void Reset();
	inline bool Advance(float x);

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
