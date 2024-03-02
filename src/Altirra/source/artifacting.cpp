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

	RecomputeNTSCTables(params);
}

void ATArtifactingEngine::BeginFrame(bool pal, bool chromaArtifacts, bool chromaArtifactHi, bool blend) {
	mbPAL = pal;
	mbChromaArtifacts = chromaArtifacts;
	mbChromaArtifactsHi = chromaArtifactHi;

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
	else if (mbChromaArtifactsHi)
		ArtifactNTSCHi(dst, src, scanlineHasHiRes);
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

namespace {
	void rotate(float& xr, float& yr, float cs, float sn) {
		float x0 = xr;
		float y0 = yr;

		xr = x0*cs + y0*sn;
		yr = -x0*sn + y0*cs;
	}

#if defined(VD_COMPILER_MSVC) && defined(VD_CPU_X86)
	void __declspec(naked) __cdecl accum_SSE2(void *rout, const void *table, const void *src, uint32 count) {
		static const __declspec(align(16)) uint64 kBiasedZero[2] = { 0x4000400040004000ull, 0x4000400040004000ull };

		__asm {
			push	ebp
			push	edi
			push	esi
			push	ebx

			mov		edx, [esp+4+16]		;dst
			mov		esi, [esp+8+16]		;table
			mov		ebx, [esp+12+16]	;src
			mov		ebp, [esp+16+16]	;count

			movdqa	xmm7, xmmword ptr kBiasedZero
			movdqa	xmm0, xmm7
			movdqa	xmm1, xmm7
			movdqa	xmm2, xmm7
			movdqa	xmm3, xmm7

			add		esi, 80h

			align	16
xloop:
			movzx	eax, byte ptr [ebx]
			shl		eax, 8
			add		eax, esi

			paddw	xmm0, [eax-80h]
			paddw	xmm1, [eax-70h]
			paddw	xmm2, [eax-60h]
			movdqa	xmm3, [eax-50h]

			movzx	eax, byte ptr [ebx+1]
			shl		eax, 8
			add		eax, esi

			paddw	xmm0, [eax-40h]
			paddw	xmm1, [eax-30h]
			paddw	xmm2, [eax-20h]
			paddw	xmm3, [eax-10h]

			movzx	eax, byte ptr [ebx+2]
			shl		eax, 8
			add		eax, esi

			paddw	xmm0, [eax]
			paddw	xmm1, [eax+10h]
			paddw	xmm2, [eax+20h]
			paddw	xmm3, [eax+30h]

			movzx	eax, byte ptr [ebx+3]
			add		ebx, 4
			shl		eax, 8
			add		eax, esi

			paddw	xmm0, [eax+40h]
			paddw	xmm1, [eax+50h]
			paddw	xmm2, [eax+60h]
			paddw	xmm3, [eax+70h]

			movdqa	[edx], xmm0
			movdqa	xmm0, xmm1
			movdqa	xmm1, xmm2
			movdqa	xmm2, xmm3

			add		edx, 16
			sub		ebp, 4
			jnz		xloop

			movdqa	[edx], xmm0
			movdqa	[edx+10h], xmm1
			movdqa	[edx+20h], xmm2

			pop		ebx
			pop		esi
			pop		edi
			pop		ebp
			ret
		}
	}

	void __declspec(naked) __cdecl accum_twin_SSE2(void *rout, const void *table, const void *src, uint32 count) {
		static const __declspec(align(16)) uint64 kBiasedZero[2] = { 0x4000400040004000ull, 0x4000400040004000ull };

		__asm {
			push	ebp
			push	edi
			push	esi
			push	ebx

			mov		edx, [esp+4+16]		;dst
			mov		esi, [esp+8+16]		;table
			mov		ebx, [esp+12+16]	;src
			mov		ebp, [esp+16+16]	;count

			movdqa	xmm7, xmmword ptr kBiasedZero
			movdqa	xmm0, xmm7
			movdqa	xmm1, xmm7
			movdqa	xmm2, xmm7
			movdqa	xmm3, xmm7

			align	16
xloop:
			movzx	eax, byte ptr [ebx]
			shl		eax, 7
			add		eax, esi

			paddw	xmm0, [eax]
			paddw	xmm1, [eax+10h]
			paddw	xmm2, [eax+20h]
			movdqa	xmm3, [eax+30h]

			movzx	eax, byte ptr [ebx+2]
			add		ebx, 4
			shl		eax, 7
			add		eax, esi

			paddw	xmm0, [eax+40h]
			paddw	xmm1, [eax+50h]
			movdqa	[edx], xmm0
			paddw	xmm2, [eax+60h]
			movdqa	xmm0, xmm1
			paddw	xmm3, [eax+70h]
			movdqa	xmm1, xmm2

			movdqa	xmm2, xmm3
			add		edx, 16
			sub		ebp, 4
			jnz		xloop

			movdqa	[edx], xmm0
			movdqa	[edx+10h], xmm1
			movdqa	[edx+20h], xmm2

			pop		ebx
			pop		esi
			pop		edi
			pop		ebp
			ret
		}
	}

	void __declspec(naked) __cdecl accum_MMX(void *rout, const void *table, const void *src, uint32 count) {
		static const __declspec(align(8)) uint64 kBiasedZero = 0x4000400040004000ull;

		__asm {
			push	ebp
			push	edi
			push	esi
			push	ebx

			mov		edx, [esp+4+16]		;dst
			mov		esi, [esp+8+16]		;table
			mov		ebx, [esp+12+16]	;src
			mov		ebp, [esp+16+16]	;count

			movq	mm7, kBiasedZero
			movq	mm0, mm7
			movq	mm1, mm7
			movq	mm2, mm7
			movq	mm3, mm7
			movq	mm4, mm7

			align	16
xloop:
			movzx	eax, byte ptr [ebx]
			imul	ecx, eax, 60h
			add		ecx, esi

			paddw	mm0, [ecx]
			paddw	mm1, [ecx+8]
			paddw	mm2, [ecx+16]
			paddw	mm3, [ecx+24]
			paddw	mm4, [ecx+32]
			movq	mm5, [ecx+40]

			movzx	eax, byte ptr [ebx+1]
			add		ebx, 2
			imul	ecx, eax, 60h
			add		ecx, esi

			paddw	mm0, [ecx+48]
			movq	[edx], mm0
			paddw	mm1, [ecx+56]
			paddw	mm2, [ecx+64]
			movq	mm0, mm1
			paddw	mm3, [ecx+72]
			movq	mm1, mm2
			paddw	mm4, [ecx+80]
			movq	mm2, mm3
			paddw	mm5, [ecx+88]

			movq	mm3, mm4
			movq	mm4, mm5

			add		edx, 8
			sub		ebp, 2
			jnz		xloop

			movq	[edx], mm0
			movq	[edx+8], mm1
			movq	[edx+16], mm2
			movq	[edx+24], mm3
			movq	[edx+32], mm4

			emms
			pop		ebx
			pop		esi
			pop		edi
			pop		ebp
			ret
		}
	}

	void __declspec(naked) __cdecl accum_twin_MMX(void *rout, const void *table, const void *src, uint32 count) {
		static const __declspec(align(8)) uint64 kBiasedZero = 0x4000400040004000ull;

		__asm {
			push	ebp
			push	edi
			push	esi
			push	ebx

			mov		edx, [esp+4+16]		;dst
			mov		esi, [esp+8+16]		;table
			mov		ebx, [esp+12+16]	;src
			mov		ebp, [esp+16+16]	;count

			movq	mm7, kBiasedZero
			movq	mm0, mm7
			movq	mm1, mm7
			movq	mm2, mm7
			movq	mm3, mm7
			movq	mm4, mm7

			align	16
xloop:
			movzx	eax, byte ptr [ebx]
			add		ebx, 2
			imul	ecx, eax, 30h
			add		ecx, esi

			paddw	mm0, [ecx]
			paddw	mm1, [ecx+8]
			movq	[edx], mm0
			paddw	mm2, [ecx+16]
			movq	mm0, mm1
			paddw	mm3, [ecx+24]
			movq	mm1, mm2
			paddw	mm4, [ecx+32]
			movq	mm2, mm3
			movq	mm5, [ecx+40]

			movq	mm3, mm4
			movq	mm4, mm5

			add		edx, 8
			sub		ebp, 2
			jnz		xloop

			movq	[edx], mm0
			movq	[edx+8], mm1
			movq	[edx+16], mm2
			movq	[edx+24], mm3
			movq	[edx+32], mm4

			emms
			pop		ebx
			pop		esi
			pop		edi
			pop		ebp
			ret
		}
	}

	void __declspec(naked) __cdecl final_MMX(void *dst, const void *srcr, const void *srcg, const void *srcb, uint32 count) {
		static const uint64 k3ff03ff03ff03ff0 = 0x3ff03ff03ff03ff0ull;
		__asm {
			push	ebp
			push	edi
			push	esi
			push	ebx

			mov		edi, [esp+4+16]		;dst
			mov		ebx, [esp+8+16]		;srcr
			mov		ecx, [esp+12+16]	;srcg
			mov		edx, [esp+16+16]	;srcb
			mov		esi, [esp+20+16]	;count

			movq	mm7, qword ptr k3ff03ff03ff03ff0
xloop:
			movq	mm0, [ebx]		;red
			add		ebx, 8
			movq	mm1, [ecx]		;green
			add		ecx, 8
			movq	mm2, [edx]		;blue
			add		edx, 8

			psubw	mm0, mm7
			psubw	mm1, mm7
			psubw	mm2, mm7
			psraw	mm0, 5
			psraw	mm1, 5
			psraw	mm2, 5
			packuswb	mm0, mm0
			packuswb	mm1, mm1
			packuswb	mm2, mm2

			punpcklbw	mm2, mm1	;gb
			punpcklbw	mm0, mm1	;gr
			movq	mm1, mm2
			punpcklwd	mm1, mm0	;grgb
			punpckhwd	mm2, mm0
			movq	[edi], mm1
			movq	[edi+8], mm2
			add		edi, 16

			sub		esi, 2
			jnz		xloop

			emms
			pop		ebx
			pop		esi
			pop		edi
			pop		ebp
			ret
		}
	}
#endif

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
}

void ATArtifactingEngine::ArtifactNTSCHi(uint32 dst[N*2], const uint8 src[N], bool scanlineHasHiRes) {
	// We are using a 21 tap filter, so we're going to need arrays of N*2+20 (since we are
	// transforming 7MHz input to 14MHz output). However, we hold two elements in each int,
	// so we actually only need N+10 elements, which we round up to N+16. We need 8-byte
	// alignment for MMX.
	__declspec(align(16)) uint32 rout[N+16];
	__declspec(align(16)) uint32 gout[N+16];
	__declspec(align(16)) uint32 bout[N+16];

#if defined(VD_COMPILER_MSVC) && defined(VD_CPU_X86)
	if (SSE2_enabled) {
		if (scanlineHasHiRes) {
			accum_SSE2(rout, m4x.mPalToR, src, N);
			accum_SSE2(gout, m4x.mPalToG, src, N);
			accum_SSE2(bout, m4x.mPalToB, src, N);
		} else {
			accum_twin_SSE2(rout, m4x.mPalToRTwin, src, N);
			accum_twin_SSE2(gout, m4x.mPalToGTwin, src, N);
			accum_twin_SSE2(bout, m4x.mPalToBTwin, src, N);
		}
	} else if (MMX_enabled) {
		if (scanlineHasHiRes) {
			accum_MMX(rout, m2x.mPalToR, src, N);
			accum_MMX(gout, m2x.mPalToG, src, N);
			accum_MMX(bout, m2x.mPalToB, src, N);
		} else {
			accum_twin_MMX(rout, m2x.mPalToRTwin, src, N);
			accum_twin_MMX(gout, m2x.mPalToGTwin, src, N);
			accum_twin_MMX(bout, m2x.mPalToBTwin, src, N);
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
		final_MMX(dst, rout+6, gout+6, bout+6, N);
	} else
#endif
	{
		final(dst, rout+6, gout+6, bout+6, N);
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

void ATArtifactingEngine::RecomputeNTSCTables(const ATColorParams& params) {
	int chroma_to_r[16][2][10] = {0};
	int chroma_to_g[16][2][10] = {0};
	int chroma_to_b[16][2][10] = {0};

	float ca;
	float cb;

	cb = 2.54375f;
	ca = 0.6155f;

	float phadjust = -0.0171875f * nsVDMath::kfPi - nsVDMath::kfPi * 0.25f;

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

	static const float k0 = +0.12832254f;
	static const float k1 = +0.09024506f;
	static const float k2 = 0;
	static const float k3 = -0.08636116f;
	static const float k4 = -0.1174442f;
	static const float k5 = -0.078894205f;
	static const float k6 = 0;
	static const float k7 = +0.06841828f;
	static const float k8 = +0.088096194f;

	for(int i=0; i<15; ++i) {
		float chromatab[8];
		float phase = phadjust + nsVDMath::kfTwoPi * ((params.mHueStart / 360.0f) + (float)i / 15.0f * (params.mHueRange / 360.0f));

		for(int j=0; j<8; ++j) {
			float v = sinf(phase + (0.125f * nsVDMath::kfTwoPi * j));

			v *= ca / cb;

			chromatab[j] = v;
		}

		float c0 = chromatab[0];
		float c1 = chromatab[1];
		float c2 = chromatab[2];
		float c3 = chromatab[3];
		float c4 = chromatab[4];
		float c5 = chromatab[5];
		float c6 = chromatab[6];
		float c7 = chromatab[7];

		float ytab[22] = {0};
		float itab[22] = {0};
		float qtab[22] = {0};

		ytab[ 7] = 0;
		ytab[ 8] = (              1*c2 + 2*c3) * (1.0f / 16.0f);
		ytab[ 9] = (1*c0 + 2*c1 + 1*c2 + 0*c3) * (1.0f / 16.0f);
		ytab[10] = (1*c0 + 0*c1 + 2*c2 + 4*c3) * (1.0f / 16.0f);
		ytab[11] = (2*c0 + 4*c1 + 2*c2 + 0*c3) * (1.0f / 16.0f);
		ytab[12] = (2*c0 + 0*c1 + 1*c2 + 2*c3) * (1.0f / 16.0f);
		ytab[13] = (1*c0 + 2*c1 + 1*c2       ) * (1.0f / 16.0f);
		ytab[14] = (1*c0                     ) * (1.0f / 16.0f);

		float t[26];

		t[ 0] = 0;
		t[ 1] = 0;
		t[ 2] = 0;
		t[ 3] = 0;
		t[ 4] = 0;
		t[ 5] = 0;
		t[ 6] = 0;
		t[ 7] = 0;
		t[ 8] =  k8*c0;
		t[ 9] =  k6*c0 + k7*c1 + k8*c2;
		t[10] =-(k4*c0 + k5*c1 + k6*c2 + k7*c3);
		t[11] =-(k2*c0 + k3*c1 + k4*c2 + k5*c3);
		t[12] =  k0*c0 + k1*c1 + k2*c2 + k3*c3;
		t[13] =  k2*c0 + k1*c1 + k0*c2 + k1*c3;
		t[14] =-(k4*c0 + k3*c1 + k2*c2 + k1*c3);
		t[15] =-(k6*c0 + k5*c1 + k4*c2 + k3*c3);
		t[16] =  k8*c0 + k7*c1 + k6*c2 + k5*c3;
		t[17] =                  k8*c2 + k7*c3;
		t[18] = 0;
		t[19] = 0;
		t[20] = 0;
		t[21] = 0;
		t[22] = 0;
		t[23] = 0;
		t[24] = 0;
		t[25] = 0;

		float u[22];
		u[0] = 0;
		u[1] = 0;
		u[20] = 0;
		u[21] = 0;

		for(int j=0; j<18; ++j)
			u[j+2] = (t[j] + 4*t[j+2] + 6*t[j+4] + 4*t[j+6] + t[j+8]) / 32.0f;

		for(int j=0; j<20; ++j) {
			if (j & 1) {
				itab[j] = (u[j+0] + u[j+2]) * 0.5f;
				qtab[j] = u[j+1];
			} else {
				itab[j] = u[j+1];
				qtab[j] = (u[j+0] + u[j+2]) * 0.5f;
			}
		}

		int rtab[2][20];
		int gtab[2][20];
		int btab[2][20];

		for(int j=0; j<20; ++j) {
			float fy = ytab[j];
			float fi = itab[j];
			float fq = qtab[j];

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

	static const float ky0 = 0.25f;
	static const float ky1 = 0.75f;
	static const float ky2 = 1.00f;
	static const float ky3 = 1.00f;
	static const float ky4 = 0.75f;
	static const float ky5 = 0.25f;

	static const float kcy1  = k7*ky0 + k8*ky1;
	static const float kcy3  = k5*ky0 + k6*ky1 + k7*ky2 + k8*ky3;
	static const float kcy5  = k3*ky0 + k4*ky1 + k5*ky2 + k6*ky3 + k7*ky4 + k8*ky5;
	static const float kcy7  = k1*ky0 + k2*ky1 + k3*ky2 + k4*ky3 + k5*ky4 + k6*ky5;
	static const float kcy9  = k1*ky0 + k0*ky1 + k1*ky2 + k2*ky3 + k3*ky4 + k4*ky5;		// center
	static const float kcy11 = k3*ky0 + k2*ky1 + k1*ky2 + k0*ky3 + k1*ky4 + k2*ky5;
	static const float kcy13 = k5*ky0 + k4*ky1 + k3*ky2 + k2*ky3 + k1*ky4 + k0*ky5;
	static const float kcy15 = k7*ky0 + k6*ky1 + k5*ky2 + k4*ky3 + k3*ky4 + k2*ky5;
	static const float kcy17 =          k8*ky1 + k7*ky2 + k6*ky3 + k5*ky4 + k4*ky5;
	static const float kcy19 =                            k8*ky3 + k7*ky4 + k6*ky5;
	static const float kcy21 =                                              k8*ky5;

	int y_to_r[16][2][11];
	int y_to_g[16][2][11];
	int y_to_b[16][2][11];

	for(int i=0; i<16; ++i) {
		float y = (float)i * params.mContrast / 15.0f + params.mBrightness;

		float t[28];
		t[0] = 0;
		t[1] = 0;
		t[2] = 0;
		t[3] = 0;
		t[4] = 0;
		t[5] = 0;
		t[6] = 0;
		t[7] = 0;
		t[8] = y*-kcy1;
		t[9] = y*-kcy3;
		t[10] = y*kcy5;
		t[11] = y*kcy7;
		t[12] = y*-kcy9;
		t[13] = y*-kcy11;
		t[14] = y*kcy13;
		t[15] = y*kcy15;
		t[16] = y*-kcy17;
		t[17] = y*-kcy19;
		t[18] = y*kcy21;
		t[19] = 0;
		t[20] = 0;
		t[21] = 0;
		t[22] = 0;
		t[23] = 0;
		t[24] = 0;
		t[25] = 0;
		t[26] = 0;
		t[27] = 0;

		float u[24];

		u[0] = 0;
		u[1] = 0;
		u[22] = 0;
		u[23] = 0;

		for(int j=0; j<20; ++j) {
			u[j+2] = (t[j+0] + 4*t[j+2] + 6*t[j+4] + 4*t[j+6] + t[j+8]) / 32.0f;
		}

		float ytab[22] = {0};
		float itab[22] = {0};
		float qtab[22] = {0};

		for(int j=0; j<22; ++j) {
			if (j & 1) {
				itab[j] = (u[j+0] + u[j+2]) * 0.5f;
				qtab[j] = u[j+1];
			} else {
				itab[j] = u[j+1];
				qtab[j] = (u[j+0] + u[j+2]) * 0.5f;
			}
		}

		ytab[ 8] = y * (5.0f / 64.0f);
		ytab[ 9] = y * (15.0f / 64.0f);
		ytab[10] = y * (21.0f / 64.0f);
		ytab[11] = y * (31.0f / 64.0f);
		ytab[12] = y * (27.0f / 64.0f);
		ytab[13] = y * (17.0f / 64.0f);
		ytab[14] = y * (11.0f / 64.0f);
		ytab[15] = y * (1.0f / 64.0f);

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

	if (SSE2_enabled) {
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

			if (MMX_enabled) {
				for(int i=0; i<2; ++i) {
					m2x.mPalToR[idx][0][10+i] += 0x40004000;
					m2x.mPalToG[idx][0][10+i] += 0x40004000;
					m2x.mPalToB[idx][0][10+i] += 0x40004000;
				}
			}

			for(int i=0; i<12; ++i) {
				m2x.mPalToRTwin[idx][i] = m2x.mPalToR[idx][0][i] + m2x.mPalToR[idx][1][i];
				m2x.mPalToGTwin[idx][i] = m2x.mPalToG[idx][0][i] + m2x.mPalToG[idx][1][i];
				m2x.mPalToBTwin[idx][i] = m2x.mPalToB[idx][0][i] + m2x.mPalToB[idx][1][i];
			}
		}
	}
}
