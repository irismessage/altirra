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
#include <vd2/system/cpuaccel.h>
#include <vd2/system/math.h>
#include "artifacting.h"
#include "artifacting_filters.h"
#include "gtia.h"
#include "gtiatables.h"

void ATArtifactPALLuma(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
void ATArtifactPALChroma(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
void ATArtifactPALFinal(uint32 *dst, const uint32 *ybuf, const uint32 *ubuf, const uint32 *vbuf, uint32 *ulbuf, uint32 *vlbuf, uint32 n);
void ATArtifactBlend_SSE2(uint32 *dst, const uint32 *src, uint32 n);
void ATArtifactBlendExchange_SSE2(uint32 *dst, uint32 *blendDst, uint32 n);

#ifdef VD_CPU_X86
	void __cdecl ATArtifactNTSCAccum_MMX(void *rout, const void *table, const void *src, uint32 count);
	void __cdecl ATArtifactNTSCAccumTwin_MMX(void *rout, const void *table, const void *src, uint32 count);
	void __cdecl ATArtifactNTSCFinal_MMX(void *dst, const void *srcr, const void *srcg, const void *srcb, uint32 count);

	void __cdecl ATArtifactNTSCAccum_SSE2(void *rout, const void *table, const void *src, uint32 count);
	void __cdecl ATArtifactNTSCAccumTwin_SSE2(void *rout, const void *table, const void *src, uint32 count);

	void __stdcall ATArtifactPALLuma_MMX(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void __stdcall ATArtifactPALLumaTwin_MMX(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void __stdcall ATArtifactPALChroma_MMX(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void __stdcall ATArtifactPALChromaTwin_MMX(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void __stdcall ATArtifactPALFinal_MMX(uint32 *dst, const uint32 *ybuf, const uint32 *ubuf, const uint32 *vbuf, uint32 *ulbuf, uint32 *vlbuf, uint32 n);
	void __stdcall ATArtifactPALLuma_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void __stdcall ATArtifactPALLumaTwin_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void __stdcall ATArtifactPALChroma_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void __stdcall ATArtifactPALChromaTwin_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void __stdcall ATArtifactPALFinal_SSE2(uint32 *dst, const uint32 *ybuf, const uint32 *ubuf, const uint32 *vbuf, uint32 *ulbuf, uint32 *vlbuf, uint32 n);
#endif

#ifdef VD_CPU_AMD64
	void ATArtifactPALLuma_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void ATArtifactPALLumaTwin_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void ATArtifactPALChroma_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void ATArtifactPALChromaTwin_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void ATArtifactPALFinal_SSE2(uint32 *dst, const uint32 *ybuf, const uint32 *ubuf, const uint32 *vbuf, uint32 *ulbuf, uint32 *vlbuf, uint32 n);
#endif

namespace {
	const float kSaturation = 75.0f / 255.0f;
}

ATArtifactingEngine::ATArtifactingEngine()
	: mbBlendActive(false)
	, mbBlendCopy(false)
	, mbHighNTSCTablesInited(false)
	, mbHighPALTablesInited(false)
{
	mArtifactingParams.mNTSCLumaSharpness = 0.30f;
	mArtifactingParams.mNTSCChromaSharpness = 1.00f;
	mArtifactingParams.mNTSCLumaNotchQ = 3.80f;
}

ATArtifactingEngine::~ATArtifactingEngine() {
}

void ATArtifactingEngine::SetColorParams(const ATColorParams& params) {
	mColorParams = params;

	float lumaRamp[16];
	ATComputeLumaRamp(params.mLumaRampMode, lumaRamp);

	mYScale = VDRoundToInt32(params.mContrast * 65280.0f * 1024.0f);
	mYBias = VDRoundToInt32(params.mBrightness * 65280.0f * 1024.0f) + 512;
	mArtifactYScale = VDRoundToInt32(params.mArtifactBias * 65280.0f * 1024.0f / 15.0f);

	const float artphase = params.mArtifactHue * (nsVDMath::kfTwoPi / 360.0f);
	const float arti = cosf(artphase);
	const float artq = sinf(artphase);
	mArtifactRed   = (int)(65280.0f * (+ 0.9563f*arti + 0.6210f*artq) * kSaturation / 15.0f * params.mArtifactSat);
	mArtifactGreen = (int)(65280.0f * (- 0.2721f*arti - 0.6474f*artq) * kSaturation / 15.0f * params.mArtifactSat);
	mArtifactBlue  = (int)(65280.0f * (- 1.1070f*arti + 1.7046f*artq) * kSaturation / 15.0f * params.mArtifactSat);

	mChromaVectors[0][0] = 0;
	mChromaVectors[0][1] = 0;
	mChromaVectors[0][2] = 0;

	for(int j=0; j<15; ++j) {
		float i = 0;
		float q = 0;

		if (params.mbUsePALQuirks) {
			static const float kPALPhaseLookup[][4]={
				{ -1.0f,  1, -5.0f,  1 },
				{  0.0f,  1, -6.0f,  1 },
				{ -7.0f, -1, -7.0f,  1 },
				{ -6.0f, -1,  0.0f, -1 },
				{ -5.0f, -1, -1.0f, -1 },
				{ -4.0f, -1, -2.0f, -1 },
				{ -2.0f, -1, -4.0f, -1 },
				{ -1.0f, -1, -5.0f, -1 },
				{  0.0f, -1, -6.0f, -1 },
				{ -7.0f,  1, -7.0f, -1 },
				{ -5.0f,  1, -1.0f,  1 },
				{ -4.0f,  1, -2.0f,  1 },
				{ -3.0f,  1, -3.0f,  1 },
				{ -2.0f,  1, -4.0f,  1 },
				{ -1.0f,  1, -5.0f,  1 },
			};

			const float step = params.mHueRange * (nsVDMath::kfTwoPi / (15.0f * 360.0f));
			const float theta = (params.mHueStart - 33.0f) * (nsVDMath::kfTwoPi / 360.0f);

			const float *co = kPALPhaseLookup[j];

			float angle2 = theta + step * (co[0] + 3.0f);
			float angle3 = theta + step * (-co[2] - 3.0f);
			float i2 = cosf(angle2) * co[1];
			float q2 = sinf(angle2) * co[1];
			float i3 = cosf(angle3) * co[3];
			float q3 = sinf(angle3) * co[3];

			i = (i2 + i3) * 0.5f;
			q = (q2 + q3) * 0.5f;
		} else {
			float theta = nsVDMath::kfTwoPi * (params.mHueStart / 360.0f + (float)j * (params.mHueRange / (15.0f * 360.0f)));
			i = cosf(theta);
			q = sinf(theta);
		}

		double r = 65280.0f * (+ 0.9563f*i + 0.6210f*q) * params.mSaturation;
		double g = 65280.0f * (- 0.2721f*i - 0.6474f*q) * params.mSaturation;
		double b = 65280.0f * (- 1.1070f*i + 1.7046f*q) * params.mSaturation;

		mChromaVectors[j+1][0] = VDRoundToInt(r);
		mChromaVectors[j+1][1] = VDRoundToInt(g);
		mChromaVectors[j+1][2] = VDRoundToInt(b);
	}

	for(int i=0; i<16; ++i) {
		float y = (lumaRamp[i & 15] * mYScale + mYBias) / 1024.0f;

		mLumaRamp[i] = VDRoundToInt32(y);
	}

	const float gamma = 1.0f / params.mGammaCorrect;
	for(int i=0; i<256; ++i) {
		int c = i >> 4;

		float cr = (float)mChromaVectors[c][0];
		float cg = (float)mChromaVectors[c][1];
		float cb = (float)mChromaVectors[c][2];
		float y = (lumaRamp[i & 15] * mYScale + mYBias) / 1024.0f;

		cr += y;
		cg += y;
		cb += y;

		float r = cr / 65280.0f;
		float g = cg / 65280.0f;
		float b = cb / 65280.0f;

		if (r > 0.0f)
			r = powf(r, gamma);

		if (g > 0.0f)
			g = powf(g, gamma);

		if (b > 0.0f)
			b = powf(b, gamma);

		mPalette[i]	= (VDClampedRoundFixedToUint8Fast((float)r) << 16)
					+ (VDClampedRoundFixedToUint8Fast((float)g) <<  8)
					+ (VDClampedRoundFixedToUint8Fast((float)b)      );

		mGammaTable[i] = (uint8)VDRoundToInt32(powf((float)i / 255.0f, gamma) * 255.0f);
	}

	mbHighNTSCTablesInited = false;
	mbHighPALTablesInited = false;
}

void ATArtifactingEngine::SetArtifactingParams(const ATArtifactingParams& params) {
	mArtifactingParams = params;

	mbHighNTSCTablesInited = false;
	mbHighPALTablesInited = false;
}

void ATArtifactingEngine::BeginFrame(bool pal, bool chromaArtifacts, bool chromaArtifactHi, bool blend) {
	mbPAL = pal;
	mbChromaArtifacts = chromaArtifacts;
	mbChromaArtifactsHi = chromaArtifactHi;

	if (chromaArtifactHi) {
		if (pal) {
			if (!mbHighPALTablesInited)
				RecomputePALTables(mColorParams);
		} else {
			if (!mbHighNTSCTablesInited)
				RecomputeNTSCTables(mColorParams);
		}
	}

	if (pal) {
		if (chromaArtifactHi) {
#if defined(VD_CPU_AMD64)
			memset(mPALDelayLineUV, 0, sizeof mPALDelayLineUV);
#else
#if defined(VD_CPU_X86)
			if (MMX_enabled || SSE2_enabled) {
				memset(mPALDelayLineUV, 0, sizeof mPALDelayLineUV);
			} else
#endif
			{
				VDMemset32(mPALDelayLineUV, 0x20002000, sizeof(mPALDelayLineUV) / sizeof(mPALDelayLineUV[0][0]));
			}
#endif
		} else {
			memset(mPALDelayLine32, 0, sizeof mPALDelayLine32);
		}
	}

	if (blend) {
		mbBlendCopy = !mbBlendActive;
		mbBlendActive = true;
	} else {
		mbBlendActive = false;
	}
}

void ATArtifactingEngine::Artifact8(uint32 y, uint32 dst[N], const uint8 src[N], bool scanlineHasHiRes, bool temporaryUpdate) {
	if (!mbChromaArtifacts)
		BlitNoArtifacts(dst, src);
	else if (mbPAL) {
		if (mbChromaArtifactsHi)
			ArtifactPALHi(dst, src, scanlineHasHiRes, (y & 1) != 0);
		else
			ArtifactPAL8(dst, src);
	} else {
		if (mbChromaArtifactsHi)
			ArtifactNTSCHi(dst, src, scanlineHasHiRes);
		else
			ArtifactNTSC(dst, src, scanlineHasHiRes);
	}

	if (mbBlendActive && y < M) {
		uint32 *blendDst = mbChromaArtifactsHi ? mPrevFrame14MHz[y] : mPrevFrame7MHz[y];
		uint32 n = mbChromaArtifactsHi ? N*2 : N;

		if (mbBlendCopy) {
			if (!temporaryUpdate)
				memcpy(blendDst, dst, sizeof(uint32)*n);
		} else {
			if (temporaryUpdate)
				Blend(dst, blendDst, n);
			else
				BlendExchange(dst, blendDst, n);
		}
	}
}

void ATArtifactingEngine::Artifact32(uint32 y, uint32 *dst, uint32 width, bool temporaryUpdate) {
	if (mbPAL)
		ArtifactPAL32(dst, width);

	if (mbBlendActive && y < M && width <= N*2) {
		uint32 *blendDst = width > N ? mPrevFrame14MHz[y] : mPrevFrame7MHz[y];

		if (mbBlendCopy) {
			if (!temporaryUpdate)
				memcpy(blendDst, dst, sizeof(uint32)*width);
		} else {
			if (temporaryUpdate)
				Blend(dst, blendDst, width);
			else
				BlendExchange(dst, blendDst, width);
		}
	}
}

void ATArtifactingEngine::InterpolateScanlines(uint32 *dst, const uint32 *src1, const uint32 *src2, uint32 n) {
	for(uint32 i=0; i<n; ++i) {
		uint32 prev = src1[i];
		uint32 next = src2[i];
		uint32 r = (prev | next) - (((prev ^ next) & 0xfefefe) >> 1);

		r -= (r & 0xfcfcfc) >> 2;
		dst[i] = r;
	}
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
			int y = mLumaRamp[luma2[x]] + ((abs(art) * mArtifactYScale) >> 10);

			cr += artr * art + y;
			cg += artg * art + y;
			cb += artb * art + y;

			cr >>= 8;
			cg >>= 8;
			cb >>= 8;

			if (cr < 0)
				cr = 0;
			else if (cr > 255)
				cr = 255;

			if (cg < 0)
				cg = 0;
			else if (cg > 255)
				cg = 255;

			if (cb < 0)
				cb = 0;
			else if (cb > 255)
				cb = 255;

			cr = mGammaTable[cr];
			cg = mGammaTable[cg];
			cb = mGammaTable[cb];

			*dst++ = cb + (cg << 8) + (cr << 16);
		}
	}
}

namespace {
	void rotate(float& xr, float& yr, float cs, float sn) {
		float x0 = xr;
		float y0 = yr;

		xr = x0*cs + y0*sn;
		yr = -x0*sn + y0*cs;
	}

	void accum(uint32 *VDRESTRICT dst, const uint32 (*VDRESTRICT table)[2][12], const uint8 *VDRESTRICT src, uint32 count) {
		count >>= 1;

		do {
			const uint8 p0 = *src++;
			const uint8 p1 = *src++;
			const uint32 *VDRESTRICT pr0 = table[p0][0];
			const uint32 *VDRESTRICT pr1 = table[p1][1];

			dst[ 0] += pr0[ 0];
			dst[ 1] += pr0[ 1] + pr1[ 1];
			dst[ 2] += pr0[ 2] + pr1[ 2];
			dst[ 3] += pr0[ 3] + pr1[ 3];
			dst[ 4] += pr0[ 4] + pr1[ 4];
			dst[ 5] += pr0[ 5] + pr1[ 5];	// center
			dst[ 6] += pr0[ 6] + pr1[ 6];
			dst[ 7] += pr0[ 7] + pr1[ 7];
			dst[ 8] += pr0[ 8] + pr1[ 8];
			dst[ 9] += pr0[ 9] + pr1[ 9];
			dst[10] += pr0[10] + pr1[10];
			dst[11] +=           pr1[11];

			dst += 2;
		} while(--count);
	}

	void accum_twin(uint32 *VDRESTRICT dst, const uint32 (*VDRESTRICT table)[12], const uint8 *VDRESTRICT src, uint32 count) {
		count >>= 1;

		do {
			uint8 p = *src;
			src += 2;

			const uint32 *VDRESTRICT pr = table[p];

			dst[ 0] += pr[ 0];
			dst[ 1] += pr[ 1];
			dst[ 2] += pr[ 2];
			dst[ 3] += pr[ 3];
			dst[ 4] += pr[ 4];
			dst[ 5] += pr[ 5];
			dst[ 6] += pr[ 6];
			dst[ 7] += pr[ 7];
			dst[ 8] += pr[ 8];
			dst[ 9] += pr[ 9];
			dst[10] += pr[10];
			dst[11] += pr[11];

			dst += 2;
		} while(--count);
	}

	void final(void *dst, const uint32 *VDRESTRICT srcr, const uint32 *VDRESTRICT srcg, const uint32 *VDRESTRICT srcb, uint32 count) {
		uint8 *VDRESTRICT dst8 = (uint8 *)dst;
		do {
			const uint32 rp = *srcr++;
			const uint32 gp = *srcg++;
			const uint32 bp = *srcb++;
			int r0 = ((int)(rp & 0xffff) - 0x3ff0) >> 5;
			int g0 = ((int)(gp & 0xffff) - 0x3ff0) >> 5;
			int b0 = ((int)(bp & 0xffff) - 0x3ff0) >> 5;
			int r1 = (int)((rp >> 16) - 0x3ff0) >> 5;
			int g1 = (int)((gp >> 16) - 0x3ff0) >> 5;
			int b1 = (int)((bp >> 16) - 0x3ff0) >> 5;

			if (r0 < 0) r0 = 0; else if (r0 > 255) r0 = 255;
			if (g0 < 0) g0 = 0; else if (g0 > 255) g0 = 255;
			if (b0 < 0) b0 = 0; else if (b0 > 255) b0 = 255;
			if (r1 < 0) r1 = 0; else if (r1 > 255) r1 = 255;
			if (g1 < 0) g1 = 0; else if (g1 > 255) g1 = 255;
			if (b1 < 0) b1 = 0; else if (b1 > 255) b1 = 255;

			*dst8++ = (uint8)b0;
			*dst8++ = (uint8)g0;
			*dst8++ = (uint8)r0;
			*dst8++ = 0;
			*dst8++ = (uint8)b1;
			*dst8++ = (uint8)g1;
			*dst8++ = (uint8)r1;
			*dst8++ = 0;
		} while(--count);
	}

	void GammaCorrect(uint8 *VDRESTRICT dst8, uint32 N, const uint8 *VDRESTRICT gammaTab) {
		for(uint32 i=0; i<N; ++i) {
			dst8[0] = gammaTab[dst8[0]];
			dst8[1] = gammaTab[dst8[1]];
			dst8[2] = gammaTab[dst8[2]];

			dst8 += 4;
		}
	}
}

void ATArtifactingEngine::ArtifactNTSCHi(uint32 dst[N*2], const uint8 src[N], bool scanlineHasHiRes) {
	// We are using a 21 tap filter, so we're going to need arrays of N*2+20 (since we are
	// transforming 7MHz input to 14MHz output). However, we hold two elements in each int,
	// so we actually only need N+10 elements, which we round up to N+16. We need 8-byte
	// alignment for MMX.
	VDALIGN(16) uint32 rout[N+16];
	VDALIGN(16) uint32 gout[N+16];
	VDALIGN(16) uint32 bout[N+16];

#if defined(VD_COMPILER_MSVC) && defined(VD_CPU_X86)
	if (SSE2_enabled) {
		if (scanlineHasHiRes) {
			ATArtifactNTSCAccum_SSE2(rout, m4x.mPalToR, src, N);
			ATArtifactNTSCAccum_SSE2(gout, m4x.mPalToG, src, N);
			ATArtifactNTSCAccum_SSE2(bout, m4x.mPalToB, src, N);
		} else {
			ATArtifactNTSCAccumTwin_SSE2(rout, m4x.mPalToRTwin, src, N);
			ATArtifactNTSCAccumTwin_SSE2(gout, m4x.mPalToGTwin, src, N);
			ATArtifactNTSCAccumTwin_SSE2(bout, m4x.mPalToBTwin, src, N);
		}
	} else if (MMX_enabled) {
		if (scanlineHasHiRes) {
			ATArtifactNTSCAccum_MMX(rout, m2x.mPalToR, src, N);
			ATArtifactNTSCAccum_MMX(gout, m2x.mPalToG, src, N);
			ATArtifactNTSCAccum_MMX(bout, m2x.mPalToB, src, N);
		} else {
			ATArtifactNTSCAccumTwin_MMX(rout, m2x.mPalToRTwin, src, N);
			ATArtifactNTSCAccumTwin_MMX(gout, m2x.mPalToGTwin, src, N);
			ATArtifactNTSCAccumTwin_MMX(bout, m2x.mPalToBTwin, src, N);
		}
	} else
#endif
	{
		for(int i=0; i<N+16; ++i)
			rout[i] = 0x40004000;

		if (scanlineHasHiRes)
			accum(rout, m2x.mPalToR, src, N);
		else
			accum_twin(rout, m2x.mPalToRTwin, src, N);

		for(int i=0; i<N+16; ++i)
			gout[i] = 0x40004000;

		if (scanlineHasHiRes)
			accum(gout, m2x.mPalToG, src, N);
		else
			accum_twin(gout, m2x.mPalToGTwin, src, N);

		for(int i=0; i<N+16; ++i)
			bout[i] = 0x40004000;

		if (scanlineHasHiRes)
			accum(bout, m2x.mPalToB, src, N);
		else
			accum_twin(bout, m2x.mPalToBTwin, src, N);
	}

	////////////////////////// 14MHz SECTION //////////////////////////////

#if defined(VD_COMPILER_MSVC) && defined(VD_CPU_X86)
	if (MMX_enabled) {
		ATArtifactNTSCFinal_MMX(dst, rout+6, gout+6, bout+6, N);
	} else
#endif
	{
		final(dst, rout+6, gout+6, bout+6, N);
	}

	GammaCorrect((uint8 *)dst, N*2, mGammaTable);
}

void ATArtifactingEngine::ArtifactPALHi(uint32 dst[N*2], const uint8 src[N], bool scanlineHasHiRes, bool oddLine) {
	// encode to YUV
	VDALIGN(16) uint32 ybuf[32 + N];
	VDALIGN(16) uint32 ubuf[32 + N];
	VDALIGN(16) uint32 vbuf[32 + N];

	uint32 *const ulbuf = mPALDelayLineUV[0];
	uint32 *const vlbuf = mPALDelayLineUV[1];

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
	if (SSE2_enabled) {
		if (scanlineHasHiRes)
			ATArtifactPALLuma_SSE2(ybuf, src, N, &mPal8x.mPalToY[oddLine][0][0][0]);
		else
			ATArtifactPALLumaTwin_SSE2(ybuf, src, N, &mPal8x.mPalToYTwin[oddLine][0][0][0]);

		ATArtifactPALChromaTwin_SSE2(ubuf, src, N, &mPal8x.mPalToUTwin[oddLine][0][0][0]);
		ATArtifactPALChromaTwin_SSE2(vbuf, src, N, &mPal8x.mPalToVTwin[oddLine][0][0][0]);

		ATArtifactPALFinal_SSE2(dst, ybuf, ubuf, vbuf, ulbuf, vlbuf, N);
		return;
	}
#endif
	
#ifdef VD_CPU_X86
	if (MMX_enabled) {
		if (scanlineHasHiRes)
			ATArtifactPALLuma_MMX(ybuf, src, N, &mPal4x.mPalToY[oddLine][0][0][0]);
		else
			ATArtifactPALLumaTwin_MMX(ybuf, src, N, &mPal4x.mPalToYTwin[oddLine][0][0][0]);

		ATArtifactPALChromaTwin_MMX(ubuf, src, N, &mPal4x.mPalToUTwin[oddLine][0][0][0]);
		ATArtifactPALChromaTwin_MMX(vbuf, src, N, &mPal4x.mPalToVTwin[oddLine][0][0][0]);

		ATArtifactPALFinal_MMX(dst, ybuf, ubuf, vbuf, ulbuf, vlbuf, N);
		return;
	}
#endif

	{
		VDMemset32(ubuf, 0x20002000, sizeof(ubuf)/sizeof(ubuf[0]));
		VDMemset32(vbuf, 0x20002000, sizeof(vbuf)/sizeof(vbuf[0]));

		ATArtifactPALLuma(ybuf, src, N, &mPal2x.mPalToY[oddLine][0][0][0]);
		ATArtifactPALChroma(ubuf, src, N, &mPal2x.mPalToU[oddLine][0][0][0]);
		ATArtifactPALChroma(vbuf, src, N, &mPal2x.mPalToV[oddLine][0][0][0]);
		ATArtifactPALFinal(dst, ybuf, ubuf, vbuf, ulbuf, vlbuf, N);
	}
}

void ATArtifactingEngine::BlitNoArtifacts(uint32 dst[N], const uint8 src[N]) {
	for(size_t x=0; x<N; ++x)
		dst[x] = mPalette[src[x]];
}

void ATArtifactingEngine::Blend(uint32 *VDRESTRICT dst, const uint32 *VDRESTRICT src, uint32 n) {
	if (SSE2_enabled && !(((uintptr)dst | (uintptr)src) & 15) && !(n & 3)) {
		ATArtifactBlend_SSE2(dst, src, n);
		return;
	}

	for(uint32 x=0; x<n; ++x) {
		const uint32 a = dst[x];
		const uint32 b = src[x];

		dst[x] = (a|b) - (((a^b) >> 1) & 0x7f7f7f7f);
	}
}

void ATArtifactingEngine::BlendExchange(uint32 *VDRESTRICT dst, uint32 *VDRESTRICT blendDst, uint32 n) {
	if (SSE2_enabled && !(((uintptr)dst | (uintptr)blendDst) & 15) && !(n & 3)) {
		ATArtifactBlendExchange_SSE2(dst, blendDst, n);
		return;
	}

	for(uint32 x=0; x<n; ++x) {
		const uint32 a = dst[x];
		const uint32 b = blendDst[x];

		blendDst[x] = a;

		dst[x] = (a|b) - (((a^b) >> 1) & 0x7f7f7f7f);
	}
}

namespace {
	struct BiquadFilter {
		float b0;
		float b1;
		float b2;
		float a1;
		float a2;

		void Filter(float *p, size_t n) {
			float x1 = 0;
			float x2 = 0;
			float y1 = 0;
			float y2 = 0;

			while(n--) {
				const float x0 = *p;
				const float y0 = x0*b0 + x1*b1 + x2*b2 - y1*a1 - y2*a2;

				*p++ = y0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
			}
		}

		void Filter1(float *p, size_t n) {
			if (!n)
				return;

			float x1 = p[0];
			float x2 = 0;
			float y1 = x1*b0;
			float y2 = 0;

			while(--n) {
				const float x0 = p[1];
				const float y0 = x0*b0 + x1*b1 + x2*b2 - y1*a1 - y2*a2;

				*p++ = y0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
			}

			*p = x1*b1 + x2*b2 - y1*a1 - y2*a2;
		}
	};

	struct BiquadLPF : public BiquadFilter {
		BiquadLPF(float fc, float Q) {
			const float w0 = nsVDMath::kfTwoPi * fc;
			const float cos_w0 = cosf(w0);
			const float alpha = sinf(w0) / (2*Q);

			const float inv_a0 = 1.0f / (1 + alpha);
			
			a2 = (1 - alpha) * inv_a0;
			a1 = (-2*cos_w0) * inv_a0;
			b0 = (0.5f - 0.5f*cos_w0) * inv_a0;
			b1 = (1 - cos_w0) * inv_a0;
			b2 = b0;

			float gain = (b0 + b1 + b2) / (1 + a1 + a2);
		}
	};

	struct BiquadBPF : public BiquadFilter {
		BiquadBPF(float fc, float Q) {
			const float w0 = nsVDMath::kfTwoPi * fc;
			const float cos_w0 = cosf(w0);
			const float alpha = sinf(w0) / (2*Q);

			const float inv_a0 = 1.0f / (1 + alpha);
			
			a1 = (-2*cos_w0) * inv_a0;
			a2 = (1 - alpha) * inv_a0;
			b0 = alpha * inv_a0;
			b1 = 0;
			b2 = -b0;
		}
	};

	struct BiquadPeak : public BiquadFilter {
		BiquadPeak(float fc, float Q, float dbGain) {
			const float A = sqrtf(powf(10.0f, dbGain / 40.0f));
			const float w0 = nsVDMath::kfTwoPi * fc;
			const float cos_w0 = cosf(w0);
			const float alpha = sinf(w0) / (2*Q);

			const float inv_a0 = 1.0f / (1 + alpha/A);
			
			a1 = (-2*cos_w0) * inv_a0;
			a2 = (1 - alpha/A) * inv_a0;
			b0 = (1 + alpha*A) * inv_a0;
			b1 = (-2*cos_w0) * inv_a0;
			b2 = (1 - alpha*A) * inv_a0;
		}
	};

	struct BiquadNotch : public BiquadFilter {
		BiquadNotch(float fc, float Q) {
			const float w0 = nsVDMath::kfTwoPi * fc;
			const float cos_w0 = cosf(w0);
			const float alpha = sinf(w0) / (2*Q);

			const float inv_a0 = 1.0f / (1 + alpha);
			
            a1 = (-2*cos_w0) * inv_a0;
            a2 = (1 - alpha) * inv_a0;
            b0 = inv_a0;
            b1 = (-2*cos_w0) * inv_a0;
            b2 = inv_a0;
		}
	};
}

void ATArtifactingEngine::RecomputeNTSCTables(const ATColorParams& params) {
	mbHighNTSCTablesInited = true;
	mbHighPALTablesInited = false;

	int chroma_to_r[16][2][10] = {0};
	int chroma_to_g[16][2][10] = {0};
	int chroma_to_b[16][2][10] = {0};

	float ca;
	float cb;

	cb = 2.54375f;
	ca = 0.6155f;

	float phadjust = -params.mArtifactHue * (nsVDMath::kfTwoPi / 360.0f) + nsVDMath::kfPi * 1.25f;

	float cp = cosf(phadjust);
	float sp = sinf(phadjust);

	float co_ir = 0.956f;
	float co_qr = 0.621f;
	float co_ig = -0.272f;
	float co_qg = -0.647f;
	float co_ib = -1.107f;
	float co_qb = 1.704f;

	rotate(co_ir, co_qr, cp, -sp);
	rotate(co_ig, co_qg, cp, -sp);
	rotate(co_ib, co_qb, cp, -sp);

	co_ir *= cb;
	co_ig *= cb;
	co_ib *= cb;
	co_qr *= cb;
	co_qg *= cb;
	co_qb *= cb;

	const float saturationScale = params.mSaturation * 3.0f;

	float lumaRamp[16];
	ATComputeLumaRamp(params.mLumaRampMode, lumaRamp);

	for(int i=0; i<15; ++i) {
		float chromatab[4];
		float phase = phadjust + nsVDMath::kfTwoPi * ((params.mHueStart / 360.0f) + (float)i / 15.0f * (params.mHueRange / 360.0f));

		for(int j=0; j<4; ++j) {
			float v = sinf(phase + (0.25f * nsVDMath::kfTwoPi * j));

			v *= ca / cb;

			chromatab[j] = v;
		}

		float c0 = chromatab[0];
		float c1 = chromatab[1];
		float c2 = chromatab[2];
		float c3 = chromatab[3];

		float ytab[22] = {0};
		float itab[22] = {0};
		float qtab[22] = {0};

		ytab[ 7+3] = 0;
		ytab[ 8+3] = (              1*c2 + 2*c3) * (1.0f / 16.0f);
		ytab[ 9+3] = (1*c0 + 2*c1 + 1*c2 + 0*c3) * (1.0f / 16.0f);
		ytab[10+3] = (1*c0 + 0*c1 + 2*c2 + 4*c3) * (1.0f / 16.0f);
		ytab[11+3] = (2*c0 + 4*c1 + 2*c2 + 0*c3) * (1.0f / 16.0f);
		ytab[12+3] = (2*c0 + 0*c1 + 1*c2 + 2*c3) * (1.0f / 16.0f);
		ytab[13+3] = (1*c0 + 2*c1 + 1*c2       ) * (1.0f / 16.0f);
		ytab[14+3] = (1*c0                     ) * (1.0f / 16.0f);

		// multiply chroma signal by pixel pulse
		float t[26] = {0};
		t[11-1] = c3 * ((1.0f - mArtifactingParams.mNTSCChromaSharpness) / 3.0f);
		t[12-1] = c0 * ((2.0f + mArtifactingParams.mNTSCChromaSharpness) / 3.0f);
		t[13-1] = c1;
		t[14-1] = c2;
		t[15-1] = c3 * ((2.0f + mArtifactingParams.mNTSCChromaSharpness) / 3.0f);
		t[16-1] = c0 * ((1.0f - mArtifactingParams.mNTSCChromaSharpness) / 3.0f);

		// demodulate chroma axes by multiplying by sin/cos
		for(int j=0; j<26; ++j) {
			if ((j+1) & 2)
				t[j] = -t[j];
		}

		// apply low-pass filter to chroma
		float u[26] = {0};

		float tu[26] = {0};
		for(int j=8; j<26; ++j) {
			u[j] = (  1 * t[j- 6])
				 + (  0.9732320952f * t[j- 4])
				 + (  0.9732320952f * t[j- 2])
				 + (  1 * t[j])
				 + (  0.1278410428f * u[j- 2]);
		}

		for(float& y : u)
			y = y / 4 / ((2+0.9732320952f*2) / (1 - 0.1278410428f));

		// interpolate chroma
		for(int j=0; j<20; ++j) {
			if (!(j & 1)) {
				itab[j] = (u[j+2] + u[j+4])*0.625f - (u[j] + u[j+6])*0.125f;
				qtab[j] = u[j+3];
			} else {
				itab[j] = u[j+3];
				qtab[j] = (u[j+2] + u[j+4])*0.625f - (u[j] + u[j+6])*0.125f;
			}
		}

		int rtab[2][20];
		int gtab[2][20];
		int btab[2][20];

		for(int j=0; j<20; ++j) {
			float fy = ytab[j];
			float fi = itab[j] * saturationScale;
			float fq = qtab[j] * saturationScale;

			float fr0 = -fy + co_ir*fi + co_qr*fq;
			float fg0 = -fy + co_ig*fi + co_qg*fq;
			float fb0 = -fy + co_ib*fi + co_qb*fq;
			float fr1 = fy + co_ir*fi + co_qr*fq;
			float fg1 = fy + co_ig*fi + co_qg*fq;
			float fb1 = fy + co_ib*fi + co_qb*fq;

			rtab[0][j] = VDRoundToInt32(fr0 * 32.0f * 255.0f);
			gtab[0][j] = VDRoundToInt32(fg0 * 32.0f * 255.0f);
			btab[0][j] = VDRoundToInt32(fb0 * 32.0f * 255.0f);
			rtab[1][j] = VDRoundToInt32(fr1 * 32.0f * 255.0f);
			gtab[1][j] = VDRoundToInt32(fg1 * 32.0f * 255.0f);
			btab[1][j] = VDRoundToInt32(fb1 * 32.0f * 255.0f);
		}

		for(int k=0; k<2; ++k) {
			for(int j=0; j<10; ++j) {
				chroma_to_r[i+1][k][j] = rtab[k][j*2+0] + (rtab[k][j*2+1] << 16);
				chroma_to_g[i+1][k][j] = gtab[k][j*2+0] + (gtab[k][j*2+1] << 16);
				chroma_to_b[i+1][k][j] = btab[k][j*2+0] + (btab[k][j*2+1] << 16);
			}
		}
	}

	////////////////////////// 28MHz SECTION //////////////////////////////

	static const float ky0 = 0.00f;
	static const float ky1 = 0.10f;
	static const float ky2 = 0.90f;
	static const float ky3 = 0.90f;
	static const float ky4 = 0.10f;
	static const float ky5 = 0.00f;

	int y_to_r[16][2][11];
	int y_to_g[16][2][11];
	int y_to_b[16][2][11];

	float lumanotch[16] = {
		(1.0f - mArtifactingParams.mNTSCLumaSharpness) / 6.0f,
		(2.0f + mArtifactingParams.mNTSCLumaSharpness) / 6.0f,
		(2.0f + mArtifactingParams.mNTSCLumaSharpness) / 6.0f,
		(1.0f - mArtifactingParams.mNTSCLumaSharpness) / 6.0f,
	};

	BiquadNotch lumaNotchFilter(0.25f, mArtifactingParams.mNTSCLumaNotchQ);

	lumaNotchFilter.Filter(lumanotch, vdcountof(lumanotch));
	lumaNotchFilter.Filter(lumanotch, vdcountof(lumanotch));

	for(float& x : lumanotch)
		x *= 2;

	for(int i=0; i<16; ++i) {
		float y = lumaRamp[i] * params.mContrast + params.mBrightness;

		float t[30] = {0};
		t[11] = y*((1.0f - mArtifactingParams.mNTSCChromaSharpness)/3.0f);
		t[12] = y*((2.0f + mArtifactingParams.mNTSCChromaSharpness)/3.0f);
		t[13] = y*((2.0f + mArtifactingParams.mNTSCChromaSharpness)/3.0f);
		t[14] = y*((1.0f - mArtifactingParams.mNTSCChromaSharpness)/3.0f);

		for(int j=0; j<30; ++j) {
			if (!(j & 2))
				t[j] = -t[j];
		}

		std::rotate(t, t+26, t+30);

		float u[28] = {0};

		// apply low-pass filter to chroma
		for(int j=4; j<24; ++j) {
			u[j] = (  1 * t[j- 4])
				 + (  0.9732320952f * t[j- 2])
				 + (  0.9732320952f * t[j- 0])
				 + (  1 * t[j+2])
				 + (  0.1278410428f * u[j- 2]);
		}

		for(float& y : u)
			y = y / 4 / ((2+0.9732320952f*2) / (1 - 0.1278410428f));

		std::rotate(u, u+2, u+24);

		{
			float tu[28] = {0};
			memcpy(tu, u, sizeof tu);

			for(int j=4; j<24; ++j) {
				tu[j] = (  1 * u[j- 4])
					 + (  0.9732320952f * u[j- 2])
					 + (  0.9732320952f * u[j- 0])
					 + (  1 * u[j+2])
					 + (  0.1278410428f * tu[j- 2]);
			}

			for(float& y : tu)
				y = y / ((2+0.9732320952f*2) / (1 - 0.1278410428f));

			memcpy(u, tu, sizeof u);
			std::rotate(u, u+2, u+24);
		}

		float ytab[22] = {0};
		float itab[22] = {0};
		float qtab[22] = {0};

		for(int j=0; j<22; ++j) {
			if ((j & 1)) {
				itab[j] = (u[j+2] + u[j+4]) * 0.625f - (u[j] + u[j+6]) * 0.125f;
				qtab[j] = u[j+3];
			} else {
				itab[j] = u[j+3];
				qtab[j] = (u[j+2] + u[j+4]) * 0.625f - (u[j] + u[j+6]) * 0.125f;
			}
		}

		// Luma notch filter (14MHz)
		for(int j=0; j<11; ++j)
			ytab[11+j] = y * lumanotch[j];

		int rtab[2][22];
		int gtab[2][22];
		int btab[2][22];

		for(int j=0; j<22; ++j) {
			float fy = ytab[j];
			float fi = itab[j];
			float fq = qtab[j];

			float fr0 = fy + co_ir*fi + co_qr*fq;
			float fg0 = fy + co_ig*fi + co_qg*fq;
			float fb0 = fy + co_ib*fi + co_qb*fq;
			float fr1 = fy - co_ir*fi - co_qr*fq;
			float fg1 = fy - co_ig*fi - co_qg*fq;
			float fb1 = fy - co_ib*fi - co_qb*fq;

			rtab[0][j] = VDRoundToInt32(fr0 * 32.0f * 255.0f);
			gtab[0][j] = VDRoundToInt32(fg0 * 32.0f * 255.0f);
			btab[0][j] = VDRoundToInt32(fb0 * 32.0f * 255.0f);
			rtab[1][j] = VDRoundToInt32(fr1 * 32.0f * 255.0f);
			gtab[1][j] = VDRoundToInt32(fg1 * 32.0f * 255.0f);
			btab[1][j] = VDRoundToInt32(fb1 * 32.0f * 255.0f);
		}

		for(int k=0; k<2; ++k) {
			for(int j=0; j<11; ++j) {
				y_to_r[i][k][j] = rtab[k][j*2+0] + (rtab[k][j*2+1] << 16);
				y_to_g[i][k][j] = gtab[k][j*2+0] + (gtab[k][j*2+1] << 16);
				y_to_b[i][k][j] = btab[k][j*2+0] + (btab[k][j*2+1] << 16);
			}
		}
	}

#ifdef VD_CPU_AMD64
	if (false) {
#else
	if (SSE2_enabled) {
#endif
		memset(&m4x, 0, sizeof m4x);

		for(int idx=0; idx<256; ++idx) {
			int cidx = idx >> 4;
			int lidx = idx & 15;

			for(int k=0; k<4; ++k) {
				for(int i=0; i<10; ++i) {
					m4x.mPalToR[idx][k][i+k] = chroma_to_r[cidx][k&1][i] + y_to_r[lidx][k&1][i];
					m4x.mPalToG[idx][k][i+k] = chroma_to_g[cidx][k&1][i] + y_to_g[lidx][k&1][i];
					m4x.mPalToB[idx][k][i+k] = chroma_to_b[cidx][k&1][i] + y_to_b[lidx][k&1][i];
				}

				m4x.mPalToR[idx][k][10+k] = y_to_r[lidx][k&1][10];
				m4x.mPalToG[idx][k][10+k] = y_to_g[lidx][k&1][10];
				m4x.mPalToB[idx][k][10+k] = y_to_b[lidx][k&1][10];
			}

			for(int i=0; i<4; ++i) {
				m4x.mPalToR[idx][0][12+i] += 0x40004000;
				m4x.mPalToG[idx][0][12+i] += 0x40004000;
				m4x.mPalToB[idx][0][12+i] += 0x40004000;
			}

			for(int i=0; i<16; ++i) {
				m4x.mPalToRTwin[idx][0][i] = m4x.mPalToR[idx][0][i] + m4x.mPalToR[idx][1][i];
				m4x.mPalToGTwin[idx][0][i] = m4x.mPalToG[idx][0][i] + m4x.mPalToG[idx][1][i];
				m4x.mPalToBTwin[idx][0][i] = m4x.mPalToB[idx][0][i] + m4x.mPalToB[idx][1][i];
			}

			for(int i=0; i<16; ++i) {
				m4x.mPalToRTwin[idx][1][i] = m4x.mPalToR[idx][2][i] + m4x.mPalToR[idx][3][i];
				m4x.mPalToGTwin[idx][1][i] = m4x.mPalToG[idx][2][i] + m4x.mPalToG[idx][3][i];
				m4x.mPalToBTwin[idx][1][i] = m4x.mPalToB[idx][2][i] + m4x.mPalToB[idx][3][i];
			}
		}
	} else {
		memset(&m2x, 0, sizeof m2x);

		for(int idx=0; idx<256; ++idx) {
			int cidx = idx >> 4;
			int lidx = idx & 15;

			for(int k=0; k<2; ++k) {
				for(int i=0; i<10; ++i) {
					m2x.mPalToR[idx][k][i+k] = chroma_to_r[cidx][k][i] + y_to_r[lidx][k][i];
					m2x.mPalToG[idx][k][i+k] = chroma_to_g[cidx][k][i] + y_to_g[lidx][k][i];
					m2x.mPalToB[idx][k][i+k] = chroma_to_b[cidx][k][i] + y_to_b[lidx][k][i];
				}

				m2x.mPalToR[idx][k][10+k] = y_to_r[lidx][k][10];
				m2x.mPalToG[idx][k][10+k] = y_to_g[lidx][k][10];
				m2x.mPalToB[idx][k][10+k] = y_to_b[lidx][k][10];
			}

#ifdef VD_CPU_X86
			if (MMX_enabled) {
				for(int i=0; i<2; ++i) {
					m2x.mPalToR[idx][0][10+i] += 0x40004000;
					m2x.mPalToG[idx][0][10+i] += 0x40004000;
					m2x.mPalToB[idx][0][10+i] += 0x40004000;
				}
			}
#endif

			for(int i=0; i<12; ++i) {
				m2x.mPalToRTwin[idx][i] = m2x.mPalToR[idx][0][i] + m2x.mPalToR[idx][1][i];
				m2x.mPalToGTwin[idx][i] = m2x.mPalToG[idx][0][i] + m2x.mPalToG[idx][1][i];
				m2x.mPalToBTwin[idx][i] = m2x.mPalToB[idx][0][i] + m2x.mPalToB[idx][1][i];
			}
		}
	}
}

void ATArtifactingEngine::RecomputePALTables(const ATColorParams& params) {
	mbHighNTSCTablesInited = false;
	mbHighPALTablesInited = true;

	float lumaRamp[16];
	ATComputeLumaRamp(params.mLumaRampMode, lumaRamp);

	// The PAL color subcarrier is about 25% faster than the NTSC subcarrier. This
	// means that a hi-res pixel covers 5/8ths of a color cycle instead of half, which
	// is a lot less convenient than the NTSC case.
	//
	// Our process looks something like this:
	//
	//	Low-pass C to 4.43MHz (1xFsc / 0.6xHR).
	//	QAM encode C at 35.54MHz (8xFsc / 5xHR).
	//	Add Y to C to produce composite signal S.
	//	Bandlimit S to around 4.43MHz (0.125f) to produce C'.
	//	Sample every 4 pixels to extract U/V (or I/Q).
	//	Low-pass S to ~3.5MHz to produce Y'.
	//
	// All of the above is baked into a series of filter kernels that run at 1.66xFsc/1xHR.
	// This avoids the high cost of actually synthesizing an 8xFsc signal.

	const float sat1 = 0.4f;
	const float sat2 = mColorParams.mSaturation / sat1;

	const float chromaPhaseStep = -mColorParams.mHueRange * (nsVDMath::kfTwoPi / (360.0f * 15.0f));

	// UV<->RGB chroma coefficients.
	const float co_vr = 1.1402509f;
	const float co_vg = -0.5808092f;
	const float co_ug = -0.3947314f;
	const float co_ub = 2.0325203f;

	float utab[2][16];
	float vtab[2][16];
	float ytab[16];

	static const float kPALPhaseLookup[][4]={
		{  2.0f,  1, -2.0f,  1 },
		{  3.0f,  1, -3.0f,  1 },
		{ -4.0f, -1, -4.0f,  1 },
		{ -3.0f, -1,  3.0f, -1 },
		{ -2.0f, -1,  2.0f, -1 },
		{ -1.0f, -1,  1.0f, -1 },
		{  1.0f, -1, -1.0f, -1 },
		{  2.0f, -1, -2.0f, -1 },
		{  3.0f, -1, -3.0f, -1 },
		{ -4.0f,  1, -4.0f, -1 },
		{ -2.0f,  1,  2.0f,  1 },
		{ -1.0f,  1,  1.0f,  1 },
		{  0.0f,  1,  0.0f,  1 },
		{  1.0f,  1, -1.0f,  1 },
		{  2.0f,  1, -2.0f,  1 },
	};

	utab[0][0] = 0;
	utab[1][0] = 0;
	vtab[0][0] = 0;
	vtab[1][0] = 0;

	for(int i=0; i<15; ++i) {
		const float *src = kPALPhaseLookup[i];
		float t1 = src[0] * chromaPhaseStep;
		float t2 = src[2] * chromaPhaseStep;

		utab[0][i+1] = cosf(t1)*src[1];
		vtab[0][i+1] = -sinf(t1)*src[1];
		utab[1][i+1] = cosf(t2)*src[3];
		vtab[1][i+1] = -sinf(t2)*src[3];
	}

	for(int i=0; i<16; ++i) {
		ytab[i] = mColorParams.mBrightness + mColorParams.mContrast * lumaRamp[i];
	}

	ATFilterKernel kernbase;
	ATFilterKernel kerncfilt;
	ATFilterKernel kernumod;
	ATFilterKernel kernvmod;

	// Box filter representing pixel time.
	kernbase.Init(0) = 1, 1, 1, 1, 1;

	// Chroma low-pass filter. We apply this before encoding to avoid excessive bleeding
	// into luma.
	kerncfilt.Init(-5) = 
		  1.0f / 1024.0f,
		 10.0f / 1024.0f,
		 45.0f / 1024.0f,
		120.0f / 1024.0f,
		210.0f / 1024.0f,
		252.0f / 1024.0f,
		210.0f / 1024.0f,
		120.0f / 1024.0f,
		 45.0f / 1024.0f,
		 10.0f / 1024.0f,
		  1.0f / 1024.0f;

	// Modulation filters -- sine and cosine of color subcarrier. We also apply chroma
	// amplitude adjustment here.
	const float ivrt2 = 0.70710678118654752440084436210485f;
	kernumod.Init(0) = 1, ivrt2, 0, -ivrt2, -1, -ivrt2, 0, ivrt2;
	kernumod *= sat1;
	kernvmod.Init(0) = 0, ivrt2, 1, ivrt2, 0, -ivrt2, -1, -ivrt2;
	kernvmod *= sat1;

	ATFilterKernel kernysep;
	ATFilterKernel kerncsep;
	ATFilterKernel kerncdemod;

	// Luma separation filter -- just a box filter.
	kernysep.Init(-4) =
		0.5f / 8.0f,
		1.0f / 8.0f,
		1.0f / 8.0f,
		1.0f / 8.0f,
		1.0f / 8.0f,
		1.0f / 8.0f,
		1.0f / 8.0f,
		1.0f / 8.0f,
		0.5f / 8.0f;

	// Chroma separation filter -- dot with peaks of sine/cosine waves and apply box filter.
	kerncsep.Init(-16) = 1,0,0,0,-2,0,0,0,2,0,0,0,-2,0,0,0,2,0,0,0,-2,0,0,0,2,0,0,0,-2,0,0,0,1;
	kerncsep *= 1.0f / 16.0f;

	// Demodulation filter. Here we invert every other sample of extracted U and V.
	kerncdemod.Init(0) =
		 -1,
		 -1,
		 1,
		 1,
		 1,
		 1,
		 -1,
		 -1;

	kerncdemod *= sat2 * 0.5f;	// 0.5 is for chroma line averaging

	memset(&mPal8x, 0, sizeof mPal8x);

	for(int i=0; i<8; ++i) {
		const float yphase = 0;
		const float cphase = 0;

		ATFilterKernel kernbase2 = kernbase >> (5*i);
		ATFilterKernel kernsignaly = kernbase2;

		// downsample chroma and modulate
		ATFilterKernel kernsignalu = (kernbase2 * kerncfilt) ^ kernumod;
		ATFilterKernel kernsignalv = (kernbase2 * kerncfilt) ^ kernvmod;

		// extract Y via low pass filter
		ATFilterKernel kerny2y = ATFilterKernelSampleBicubic(kernsignaly * kernysep, yphase, 2.5f, -0.75f);
		ATFilterKernel kernu2y = ATFilterKernelSampleBicubic(kernsignalu * kernysep, yphase, 2.5f, -0.75f);
		ATFilterKernel kernv2y = ATFilterKernelSampleBicubic(kernsignalv * kernysep, yphase, 2.5f, -0.75f);

		// separate, low pass filter and demodulate chroma
		ATFilterKernel kerny2u = ATFilterKernelSampleBicubic(ATFilterKernelSamplePoint((kernsignaly * kerncsep) ^ kerncdemod, 0, 4), cphase     , 0.625f, -0.75f);
		ATFilterKernel kernu2u = ATFilterKernelSampleBicubic(ATFilterKernelSamplePoint((kernsignalu * kerncsep) ^ kerncdemod, 0, 4), cphase     , 0.625f, -0.75f);
		ATFilterKernel kernv2u = ATFilterKernelSampleBicubic(ATFilterKernelSamplePoint((kernsignalv * kerncsep) ^ kerncdemod, 0, 4), cphase     , 0.625f, -0.75f);

		ATFilterKernel kerny2v = ATFilterKernelSampleBicubic(ATFilterKernelSamplePoint((kernsignaly * kerncsep) ^ kerncdemod, 2, 4), cphase-0.5f, 0.625f, -0.75f);
		ATFilterKernel kernu2v = ATFilterKernelSampleBicubic(ATFilterKernelSamplePoint((kernsignalu * kerncsep) ^ kerncdemod, 2, 4), cphase-0.5f, 0.625f, -0.75f);
		ATFilterKernel kernv2v = ATFilterKernelSampleBicubic(ATFilterKernelSamplePoint((kernsignalv * kerncsep) ^ kerncdemod, 2, 4), cphase-0.5f, 0.625f, -0.75f);

		for(int k=0; k<2; ++k) {
			float v_invert = k ? -1.0f : 1.0f;

			for(int j=0; j<256; ++j) {
				float u = utab[k][j >> 4];
				float v = vtab[k][j >> 4];
				float y = ytab[j & 15];

				float p2yw[8 + 8] = {0};
				float p2uw[24 + 8] = {0};
				float p2vw[24 + 8] = {0};

				if (SSE2_enabled) {
					int ypos = 3 - (i & 4)*2;
					int cpos = 12 - (i & 4)*2;

					ATFilterKernelAccumulateWindow(kerny2y, p2yw, ypos, 16, y);
					ATFilterKernelAccumulateWindow(kernu2y, p2yw, ypos, 16, u);
					ATFilterKernelAccumulateWindow(kernv2y, p2yw, ypos, 16, v);

					ATFilterKernelAccumulateWindow(kerny2u, p2uw, cpos, 32, y * co_ub);
					ATFilterKernelAccumulateWindow(kernu2u, p2uw, cpos, 32, u * co_ub);
					ATFilterKernelAccumulateWindow(kernv2u, p2uw, cpos, 32, v * co_ub);

					ATFilterKernelAccumulateWindow(kerny2v, p2vw, cpos, 32, y * co_vr * v_invert);
					ATFilterKernelAccumulateWindow(kernu2v, p2vw, cpos, 32, u * co_vr * v_invert);
					ATFilterKernelAccumulateWindow(kernv2v, p2vw, cpos, 32, v * co_vr * v_invert);

					uint32 *kerny16 = mPal8x.mPalToY[k][j][i];
					uint32 *kernu16 = mPal8x.mPalToU[k][j][i];
					uint32 *kernv16 = mPal8x.mPalToV[k][j][i];

					for(int offset=0; offset<8; ++offset) {
						sint32 w0 = VDRoundToInt32(p2yw[offset*2+0] * 64.0f * 255.0f);
						sint32 w1 = VDRoundToInt32(p2yw[offset*2+1] * 64.0f * 255.0f);

						kerny16[offset] = (w1 << 16) + w0;
					}

					kerny16[i & 3] += 0x00200020;

					for(int offset=0; offset<16; ++offset) {
						sint32 w0 = VDRoundToInt32(p2uw[offset*2+0] * 64.0f * 255.0f);
						sint32 w1 = VDRoundToInt32(p2uw[offset*2+1] * 64.0f * 255.0f);

						kernu16[offset] = (w1 << 16) + w0;
					}

					for(int offset=0; offset<16; ++offset) {
						sint32 w0 = VDRoundToInt32(p2vw[offset*2+0] * 64.0f * 255.0f);
						sint32 w1 = VDRoundToInt32(p2vw[offset*2+1] * 64.0f * 255.0f);

						kernv16[offset] = (w1 << 16) + w0;
					}
				} else if (MMX_enabled) {
					int ypos = 3 - (i & 6)*2;
					int cpos = 12 - (i & 6)*2;

					ATFilterKernelAccumulateWindow(kerny2y, p2yw, ypos, 12, y);
					ATFilterKernelAccumulateWindow(kernu2y, p2yw, ypos, 12, u);
					ATFilterKernelAccumulateWindow(kernv2y, p2yw, ypos, 12, v);

					ATFilterKernelAccumulateWindow(kerny2u, p2uw, cpos, 28, y * co_ub);
					ATFilterKernelAccumulateWindow(kernu2u, p2uw, cpos, 28, u * co_ub);
					ATFilterKernelAccumulateWindow(kernv2u, p2uw, cpos, 28, v * co_ub);

					ATFilterKernelAccumulateWindow(kerny2v, p2vw, cpos, 28, y * co_vr * v_invert);
					ATFilterKernelAccumulateWindow(kernu2v, p2vw, cpos, 28, u * co_vr * v_invert);
					ATFilterKernelAccumulateWindow(kernv2v, p2vw, cpos, 28, v * co_vr * v_invert);

					uint32 *kerny16 = mPal4x.mPalToY[k][j][i];
					uint32 *kernu16 = mPal4x.mPalToU[k][j][i];
					uint32 *kernv16 = mPal4x.mPalToV[k][j][i];

					for(int offset=0; offset<6; ++offset) {
						sint32 w0 = VDRoundToInt32(p2yw[offset*2+0] * 64.0f * 255.0f);
						sint32 w1 = VDRoundToInt32(p2yw[offset*2+1] * 64.0f * 255.0f);

						kerny16[offset] = (w1 << 16) + w0;
					}

					for(int offset=0; offset<14; ++offset) {
						sint32 w0 = VDRoundToInt32(p2uw[offset*2+0] * 64.0f * 255.0f);
						sint32 w1 = VDRoundToInt32(p2uw[offset*2+1] * 64.0f * 255.0f);

						kernu16[offset] = (w1 << 16) + w0;
					}

					for(int offset=0; offset<14; ++offset) {
						sint32 w0 = VDRoundToInt32(p2vw[offset*2+0] * 64.0f * 255.0f);
						sint32 w1 = VDRoundToInt32(p2vw[offset*2+1] * 64.0f * 255.0f);

						kernv16[offset] = (w1 << 16) + w0;
					}
				} else {
					int ypos = 3 - i*2;
					int cpos = 12 - i*2;

					ATFilterKernelAccumulateWindow(kerny2y, p2yw, ypos, 8, y);
					ATFilterKernelAccumulateWindow(kernu2y, p2yw, ypos, 8, u);
					ATFilterKernelAccumulateWindow(kernv2y, p2yw, ypos, 8, v);

					ATFilterKernelAccumulateWindow(kerny2u, p2uw, cpos, 24, y * co_ub);
					ATFilterKernelAccumulateWindow(kernu2u, p2uw, cpos, 24, u * co_ub);
					ATFilterKernelAccumulateWindow(kernv2u, p2uw, cpos, 24, v * co_ub);

					ATFilterKernelAccumulateWindow(kerny2v, p2vw, cpos, 24, y * co_vr * v_invert);
					ATFilterKernelAccumulateWindow(kernu2v, p2vw, cpos, 24, u * co_vr * v_invert);
					ATFilterKernelAccumulateWindow(kernv2v, p2vw, cpos, 24, v * co_vr * v_invert);

					uint32 *kerny16 = mPal2x.mPalToY[k][j][i];
					uint32 *kernu16 = mPal2x.mPalToU[k][j][i];
					uint32 *kernv16 = mPal2x.mPalToV[k][j][i];

					for(int offset=0; offset<4; ++offset) {
						sint32 w0 = VDRoundToInt32(p2yw[offset*2+0] * 64.0f * 255.0f);
						sint32 w1 = VDRoundToInt32(p2yw[offset*2+1] * 64.0f * 255.0f);

						kerny16[offset] = (w1 << 16) + w0;
					}

					kerny16[3] += 0x40004000;

					for(int offset=0; offset<12; ++offset) {
						sint32 w0 = VDRoundToInt32(p2uw[offset*2+0] * 64.0f * 255.0f);
						sint32 w1 = VDRoundToInt32(p2uw[offset*2+1] * 64.0f * 255.0f);

						kernu16[offset] = (w1 << 16) + w0;
					}

					for(int offset=0; offset<12; ++offset) {
						sint32 w0 = VDRoundToInt32(p2vw[offset*2+0] * 64.0f * 255.0f);
						sint32 w1 = VDRoundToInt32(p2vw[offset*2+1] * 64.0f * 255.0f);

						kernv16[offset] = (w1 << 16) + w0;
					}
				}
			}
		}
	}

	// Create twin kernels.
	//
	// Most scanlines on the Atari aren't hires, which means that pairs of pixels are identical.
	// What we do here is precompute pairs of adjacent phase filter kernels added together. On
	// any scanline that is lores only, we can use a faster twin-mode set of filter routines.

	if (SSE2_enabled) {
		for(int i=0; i<2; ++i) {
			for(int j=0; j<256; ++j) {
				for(int k=0; k<4; ++k) {
					for(int l=0; l<8; ++l)
						mPal8x.mPalToYTwin[i][j][k][l] = mPal8x.mPalToY[i][j][k*2][l] + mPal8x.mPalToY[i][j][k*2+1][l];

					for(int l=0; l<16; ++l)
						mPal8x.mPalToUTwin[i][j][k][l] = mPal8x.mPalToU[i][j][k*2][l] + mPal8x.mPalToU[i][j][k*2+1][l];

					for(int l=0; l<16; ++l)
						mPal8x.mPalToVTwin[i][j][k][l] = mPal8x.mPalToV[i][j][k*2][l] + mPal8x.mPalToV[i][j][k*2+1][l];
				}
			}
		}
	} else if (MMX_enabled) {
		for(int i=0; i<2; ++i) {
			for(int j=0; j<256; ++j) {
				for(int k=0; k<4; ++k) {
					for(int l=0; l<6; ++l)
						mPal4x.mPalToYTwin[i][j][k][l] = mPal4x.mPalToY[i][j][k*2][l] + mPal4x.mPalToY[i][j][k*2+1][l];

					for(int l=0; l<16; ++l)
						mPal4x.mPalToUTwin[i][j][k][l] = mPal4x.mPalToU[i][j][k*2][l] + mPal4x.mPalToU[i][j][k*2+1][l];

					for(int l=0; l<16; ++l)
						mPal4x.mPalToVTwin[i][j][k][l] = mPal4x.mPalToV[i][j][k*2][l] + mPal4x.mPalToV[i][j][k*2+1][l];
				}
			}
		}
	}
}

