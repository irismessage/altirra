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
#include "gtia.h"

namespace {
	const float kSaturation = 75.0f / 255.0f;
}

ATArtifactingEngine::ATArtifactingEngine()
	: mbBlendActive(false)
	, mbBlendCopy(false)
{
}

ATArtifactingEngine::~ATArtifactingEngine() {
}

void ATArtifactingEngine::SetColorParams(const ATColorParams& params) {
	mYScale = VDRoundToInt32(params.mContrast * 65280.0f * 1024.0f / 15.0f);
	mYBias = VDRoundToInt32(params.mBrightness * 65280.0f * 1024.0f) + 512;
	mArtifactYScale = VDRoundToInt32(params.mArtifactBias * 65280.0f * 1024.0f / 15.0f);

	const float artphase = params.mArtifactHue * (nsVDMath::kfTwoPi / 360.0f);
	const float arti = cosf(artphase);
	const float artq = -sinf(artphase);
	mArtifactRed   = (int)(65280.0f * (+ 0.9563f*arti + 0.6210f*artq) * kSaturation / 15.0f * params.mArtifactSat);
	mArtifactGreen = (int)(65280.0f * (- 0.2721f*arti - 0.6474f*artq) * kSaturation / 15.0f * params.mArtifactSat);
	mArtifactBlue  = (int)(65280.0f * (- 1.1070f*arti + 1.7046f*artq) * kSaturation / 15.0f * params.mArtifactSat);

	mChromaVectors[0][0] = 0;
	mChromaVectors[0][1] = 0;
	mChromaVectors[0][2] = 0;

	for(int j=0; j<15; ++j) {
		float theta = nsVDMath::kfTwoPi * (params.mHueStart / 360.0f + (float)j * (params.mHueRange / (15.0f * 360.0f)));
		float i = cosf(theta);
		float q = sinf(theta);
		double r = 65280.0f * (+ 0.9563f*i + 0.6210f*q) * params.mSaturation;
		double g = 65280.0f * (- 0.2721f*i - 0.6474f*q) * params.mSaturation;
		double b = 65280.0f * (- 1.1070f*i + 1.7046f*q) * params.mSaturation;

		mChromaVectors[j+1][0] = VDRoundToInt(r);
		mChromaVectors[j+1][1] = VDRoundToInt(g);
		mChromaVectors[j+1][2] = VDRoundToInt(b);
	}

	for(int i=0; i<256; ++i) {
		int c = i >> 4;

		int cr = mChromaVectors[c][0];
		int cg = mChromaVectors[c][1];
		int cb = mChromaVectors[c][2];
		int y = ((i & 15) * mYScale + mYBias) >> 10;

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
		cr &= 0xff;

		cg &= ~cg >> 31;
		t = (255 - cg);
		cg |= (t >> 31);
		cg &= 0xff;

		cb &= ~cb >> 31;
		t = (255 - cb);
		cb |= (t >> 31);
		cb &= 0xff;

		mPalette[i] = cb + (cg << 8) + (cr << 16);
	}
}

void ATArtifactingEngine::BeginFrame(bool pal, bool chromaArtifacts, bool blend) {
	mbPAL = pal;
	mbChromaArtifacts = chromaArtifacts;

	if (pal)
		memset(mPALDelayLine32, 0, sizeof mPALDelayLine32);

	if (blend) {
		mbBlendCopy = !mbBlendActive;
		mbBlendActive = true;
	} else {
		mbBlendActive = false;
	}
}

void ATArtifactingEngine::Artifact8(uint32 y, uint32 dst[N], const uint8 src[N], bool scanlineHasHiRes) {
	if (!mbChromaArtifacts)
		BlitNoArtifacts(dst, src);
	else if (mbPAL)
		ArtifactPAL8(dst, src);
	else
		ArtifactNTSC(dst, src, scanlineHasHiRes);

	if (mbBlendActive && y < M) {
		uint32 *blendDst = mPrevFrame[y];

		if (mbBlendCopy)
			memcpy(blendDst, dst, sizeof(uint32)*N);
		else
			BlendExchange(dst, blendDst);
	}
}

void ATArtifactingEngine::Artifact32(uint32 y, uint32 dst[N], uint32 width) {
	if (mbPAL)
		ArtifactPAL32(dst, width);
}

void ATArtifactingEngine::ArtifactPAL8(uint32 dst[N], const uint8 src[N]) {
	for(int i=0; i<N; ++i) {
		uint8 prev = mPALDelayLine[i];
		uint8 next = src[i];
		uint32 prevColor = mPalette[(prev & 0xf0) + (next & 0x0f)];
		uint32 nextColor = mPalette[next];

		dst[i] = (prevColor | nextColor) - (((prevColor ^ nextColor) & 0xfefefe) >> 1);
	}

	memcpy(mPALDelayLine, src, sizeof mPALDelayLine);
}

void ATArtifactingEngine::ArtifactPAL32(uint32 *dst, uint32 width) {
	uint8 *dst8 = (uint8 *)dst;
	uint8 *delay8 = mPALDelayLine32;

	for(uint32 i=0; i<width; ++i) {
		// avg = (prev + next)/2
		// result = dot(next, lumaAxis) + avg - dot(avg, lumaAxis)
		//        = avg + dot(next - avg, lumaAxis)
		//        = avg + dot(next - prev/2 - next/2, lumaAxis)
		//        = avg + dot(next - prev, lumaAxis/2)
		int b1 = delay8[0];
		int g1 = delay8[1];
		int r1 = delay8[2];

		int b2 = dst8[0];
		int g2 = dst8[1];
		int r2 = dst8[2];
		delay8[0] = b2;
		delay8[1] = g2;
		delay8[2] = r2;
		delay8 += 3;

		int adj = ((b2 - b1) * 54 + (g2 - g1) * 183 + (b2 - b1) * 19 + 256) >> 9;
		int rf = ((r1 + r2 + 1) >> 1) + adj;
		int gf = ((g1 + g2 + 1) >> 1) + adj;
		int bf = ((b1 + b2 + 1) >> 1) + adj;

		if ((unsigned)rf >= 256)
			rf = (~rf >> 31);

		if ((unsigned)gf >= 256)
			gf = (~gf >> 31);

		if ((unsigned)bf >= 256)
			bf = (~bf >> 31);

		dst8[0] = (uint8)bf;
		dst8[1] = (uint8)gf;
		dst8[2] = (uint8)rf;
		dst8 += 4;
	}
}

void ATArtifactingEngine::ArtifactNTSC(uint32 dst[N], const uint8 src[N], bool scanlineHasHiRes) {
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
	const int artr = mArtifactRed;
	const int artg = mArtifactGreen;
	const int artb = mArtifactBlue;

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
			int y = (luma2[x] * mYScale + mYBias + abs(art) * mArtifactYScale) >> 10;

			cr += artr * art + y;
			cg += artg * art + y;
			cb += artb * art + y;

			cr >>= 8;
			cg >>= 8;
			cb >>= 8;

			int t;

			t = (255 - cr);
			cr &= ~cr >> 31;
			cr |= (t >> 31);
			cr &= 255;

			t = (255 - cg);
			cg &= ~cg >> 31;
			cg |= (t >> 31);
			cg &= 255;

			t = (255 - cb);
			cb &= ~cb >> 31;
			cb |= (t >> 31);
			cb &= 255;

			*dst++ = cb + (cg << 8) + (cr << 16);
		}
	}
}

void ATArtifactingEngine::BlitNoArtifacts(uint32 dst[N], const uint8 src[N]) {
	for(int x=0; x<N; ++x)
		dst[x] = mPalette[src[x]];
}

void ATArtifactingEngine::BlendExchange(uint32 dst[N], uint32 blendDst[N]) {
	for(int x=0; x<N; ++x) {
		const uint32 a = dst[x];
		const uint32 b = blendDst[x];

		blendDst[x] = a;

		dst[x] = (a|b) - (((a^b) >> 1) & 0x7f7f7f7f);
	}
}
