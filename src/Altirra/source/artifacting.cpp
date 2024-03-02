//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009 Avery Lee
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

#include "stdafx.h"
#include <vd2/system/math.h>
#include "artifacting.h"

namespace {
	const float kSaturation = 75.0f / 255.0f;
}

ATArtifactingEngine::ATArtifactingEngine() {
	mChromaVectors[0][0] = 0;
	mChromaVectors[0][1] = 0;
	mChromaVectors[0][2] = 0;

	for(int j=0; j<15; ++j) {
		float theta = nsVDMath::kfTwoPi * (-15.0f / 360.0f + (float)j / 14.4f);
		float i = cosf(theta);
		float q = sinf(theta);

		mChromaVectors[j+1][0] = VDRoundToInt(65536.0f * (+ 0.9563f*i + 0.6210f*q) * kSaturation);
		mChromaVectors[j+1][1] = VDRoundToInt(65536.0f * (- 0.2721f*i - 0.6474f*q) * kSaturation);
		mChromaVectors[j+1][2] = VDRoundToInt(65536.0f * (- 1.1070f*i + 1.7046f*q) * kSaturation);
	}

	for(int i=0; i<256; ++i) {
		int c = i >> 4;

		int cr = mChromaVectors[c][0];
		int cg = mChromaVectors[c][1];
		int cb = mChromaVectors[c][2];
		int y = (i & 15) * (65535 / 15);

		cr += y;
		cg += y;
		cb += y;

		cr >>= 8;
		cg >>= 8;
		cb >>= 8;

		int t;

		cr &= ~cr >> 31;
		t = (255 - cr);
		cr |= (t >> 31);

		cg &= ~cg >> 31;
		t = (255 - cg);
		cg |= (t >> 31);

		cb &= ~cb >> 31;
		t = (255 - cb);
		cb |= (t >> 31);

		mPalette[i] = cb + (cg << 8) + (cr << 16);
	}
}

ATArtifactingEngine::~ATArtifactingEngine() {
}

void ATArtifactingEngine::Artifact(uint32 dst[N], const uint8 src[N], bool scanlineHasHiRes) {
	if (!scanlineHasHiRes) {
		BlitNoArtifacts(dst, src);
		return;
	}

	uint8 luma[N + 4];
	uint8 luma2[N];
	sint8 inv[N];

	for(int i=0; i<N; ++i)
		luma[i+2] = src[i] & 15;

	luma[0] = luma[1] = luma[2];
	luma[N+2] = luma[N+3] = luma[N+1];

	int artsum = 0;
	for(int i=0; i<N; ++i) {
		int y0 = luma[i+1];
		int y1 = luma[i+2];
		int y2 = luma[i+3];

		int d = 0;

		if (y1 < y0 && y1 < y2) {
			if (y0 < y2)
				d = y1 - y0;
			else
				d = y1 - y2;
		} else if (y1 > y0 && y1 > y2) {
			if (y0 > y2)
				d = y1 - y0;
			else
				d = y1 - y2;
		}

		if (i & 1)
			d = -d;

		artsum |= d;

		inv[i] = (sint8)d;

		if (d)
			luma2[i] = (y0 + 2*y1 + y2 + 2) >> 2;
		else
			luma2[i] = y1;
	}

	if (!artsum) {
		BlitNoArtifacts(dst, src);
		return;
	}

	// This has no physical basis -- it just looks OK.
	const float artphase = 2.0f;
	const float arti = cosf(artphase);
	const float artq = -sinf(artphase);
	const int artr = VDRoundToInt(65536.0f * (+ 0.9563f*arti + 0.6210f*artq) * kSaturation / 15.0f);
	const int artg = VDRoundToInt(65536.0f * (- 0.2721f*arti - 0.6474f*artq) * kSaturation / 15.0f);
	const int artb = VDRoundToInt(65536.0f * (- 1.1070f*arti + 1.7046f*artq) * kSaturation / 15.0f);

	for(int x=0; x<N; ++x) {
		uint8 p = src[x];
		int art = inv[x]; 

		if (!art) {
			*dst++ = mPalette[p];
		} else {
			int c = p >> 4;

			int cr = mChromaVectors[c][0];
			int cg = mChromaVectors[c][1];
			int cb = mChromaVectors[c][2];
			int y = luma2[x] * (65535 / 15);

			cr += artr * art + y;
			cg += artg * art + y;
			cb += artb * art + y;

			cr >>= 8;
			cg >>= 8;
			cb >>= 8;

			int t;

			cr &= ~cr >> 31;
			t = (255 - cr);
			cr |= (t >> 31);

			cg &= ~cg >> 31;
			t = (255 - cg);
			cg |= (t >> 31);

			cb &= ~cb >> 31;
			t = (255 - cb);
			cb |= (t >> 31);

			*dst++ = cb + (cg << 8) + (cr << 16);
		}
	}
}

void ATArtifactingEngine::BlitNoArtifacts(uint32 dst[N], const uint8 src[N]) {
	for(int x=0; x<N; ++x)
		dst[x] = mPalette[src[x]];
}

