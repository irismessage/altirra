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

////////////////////////////////////////////////////////////////////////////////
// Artifacting engine
//
// The artifacting engine is essentially the software postprocessing engine for
// the emulator. It provides the following post-process effects:
//
//	- Conversion from indexed color to RGB
//	- Even/odd luma-chroma artifacts (NTSC artifacting)
//	- Chroma line blending (PAL artifacting)
//	- Full chroma artifacting (NTSC/PAL high artifacting)
//	- Frame blending
//	- Scanline rendering
//
// The artifacting engine can also work with the hardware screenFX support to
// split post-processing between software and hardware paths.
//
//
// Expanded range processing
// -------------------------
// The artifacting engine has a notion of 'expanded range' where colors are
// encoded in the source color space in [-0.5, 1.5] within 8-bit instead of
// 0-1. This allows capturing of colors that would otherwise be lost. For NTSC,
// this allows preservation of color values above 1 in NTSC color that can
// still be represented in WCG/HDR; for PAL, it preserves colors that can be
// brought inside of sRGB by chroma blending. Some of these colors are
// 'impossible' in linear sRGB or scRGB encoding due to negative values, and
// thus the signed encoding in gamma space rather than a traditional linear
// color space pipeline.
//
// Expanded range is designed to be fast to convert to and from normal range
// encoding, as follows:
//
//	expanded = normal/2 + 64
//	normal = expanded*2 - 128
//
// Note that the bias is thus not exactly 0.5, it is actually 64/255. This is
// the value used when processing expanded range in the screenFX pipeline for
// hardware accelerated chroma line blending or HDR output.

#include <stdafx.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/math.h>
#include <vd2/system/vecmath.h>
#include <vd2/system/vectors.h>
#include <vd2/system/zip.h>
#include <at/atcore/consoleoutput.h>
#include "artifacting.h"
#include "artifacting_filters.h"
#include "gtia.h"
#include "gtiatables.h"

void ATArtifactPALLuma(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
void ATArtifactPALChroma(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
void ATArtifactPALFinal(uint32 *dst, const uint32 *ybuf, const uint32 *ubuf, const uint32 *vbuf, uint32 *ulbuf, uint32 *vlbuf, uint32 n);
void ATArtifactPALFinalMono(uint32 *dst, const uint32 *ybuf, uint32 n, const uint32 *monoTable);
void ATArtifactPAL32(void *dst, void *delayLine, uint32 n, bool useSignedPalette);

#if VD_CPU_X86 || VD_CPU_X64
	void ATArtifactBlend_SSE2(uint32 *dst, const uint32 *src, uint32 n);
	void ATArtifactBlendLinear_SSE2(uint32 *dst, const uint32 *src, uint32 n, bool extendedRange);
	void ATArtifactBlendExchange_SSE2(uint32 *dst, uint32 *blendDst, uint32 n);
	void ATArtifactBlendExchangeLinear_SSE2(uint32 *dst, uint32 *blendDst, uint32 n, bool extendedRange);
	void ATArtifactBlendScanlines_SSE2(uint32 *dst0, const uint32 *src10, const uint32 *src20, uint32 n, float intensity);
	void ATArtifactNTSCFinal_SSE2(void *dst, const void *srcr, const void *srcg, const void *srcb, uint32 count);
	void ATArtifactPAL32_SSE2(void *dst, void *delayLine, uint32 n, bool useSignedPalette);
#endif

#ifdef VD_CPU_X86
	void __cdecl ATArtifactNTSCAccum_SSE2(void *rout, const void *table, const void *src, uint32 count);
	void __cdecl ATArtifactNTSCAccumTwin_SSE2(void *rout, const void *table, const void *src, uint32 count);

	void __stdcall ATArtifactPALLuma_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void __stdcall ATArtifactPALLumaTwin_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void __stdcall ATArtifactPALChroma_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void __stdcall ATArtifactPALChromaTwin_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void __stdcall ATArtifactPALFinal_SSE2(uint32 *dst, const uint32 *ybuf, const uint32 *ubuf, const uint32 *vbuf, uint32 *ulbuf, uint32 *vlbuf, uint32 n);
	void ATArtifactPALFinalMono_SSE2(uint32 *dst, const uint32 *ybuf, uint32 n, const vdfloat3& monoColor);
#endif

#ifdef VD_CPU_AMD64
	void ATArtifactNTSCAccum_SSE2(void *rout, const void *table, const void *src, uint32 count);
	void ATArtifactNTSCAccumTwin_SSE2(void *rout, const void *table, const void *src, uint32 count);

	void ATArtifactPALLuma_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void ATArtifactPALLumaTwin_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void ATArtifactPALChroma_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void ATArtifactPALChromaTwin_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
	void ATArtifactPALFinalMono_SSE2(uint32 *dst, const uint32 *ybuf, uint32 n, const vdfloat3& monoColor);
	void ATArtifactPALFinal_SSE2(uint32 *dst, const uint32 *ybuf, const uint32 *ubuf, const uint32 *vbuf, uint32 *ulbuf, uint32 *vlbuf, uint32 n);
#endif

#ifdef VD_CPU_ARM64
	void ATArtifactBlend_NEON(uint32 *dst, const uint32 *src, uint32 n);
	void ATArtifactBlendExchange_NEON(uint32 *dst, uint32 *blendDst, uint32 n);
	void ATArtifactBlendLinear_NEON(uint32 *dst, const uint32 *src, uint32 n, bool extendedRange);
	void ATArtifactBlendExchangeLinear_NEON(uint32 *dst, uint32 *blendDst, uint32 n, bool extendedRange);
	void ATArtifactBlendScanlines_NEON(uint32 *dst0, const uint32 *src10, const uint32 *src20, uint32 n, float intensity);

	void ATArtifactNTSCAccum_NEON(void *rout, const void *table, const void *src, uint32 count);
	void ATArtifactNTSCAccumTwin_NEON(void *rout, const void *table, const void *src, uint32 count);
	void ATArtifactNTSCFinal_NEON(void *dst0, const void *srcr0, const void *srcg0, const void *srcb0, uint32 count);
#endif

namespace {
	constexpr float kSaturation = 75.0f / 255.0f;

	// Unless we are showing horizontal blank, we only need to process pixels within
	// the visible range of $22-DD in color clocks, or twice that in hires pixels (7MHz
	// dot clock).
	constexpr int kLeftBorder7MHz = 34*2;
	constexpr int kRightBorder7MHz = 222*2;
	constexpr int kLeftBorder14MHz = kLeftBorder7MHz*2;
	constexpr int kRightBorder14MHz = kRightBorder7MHz*2;

	// These versions are for the display rect, expanded to the nearest multiples of 2/4/8/16. (They
	// do not currently differ.)
	constexpr int kLeftBorder7MHz_2 = kLeftBorder7MHz & ~1;
	constexpr int kRightBorder7MHz_2 = (kRightBorder7MHz + 1) & ~1;
	constexpr int kLeftBorder14MHz_2 = kLeftBorder14MHz & ~1;
	constexpr int kRightBorder14MHz_2 = (kRightBorder14MHz + 1) & ~1;

	constexpr int kLeftBorder7MHz_4 = kLeftBorder7MHz & ~3;
	constexpr int kRightBorder7MHz_4 = (kRightBorder7MHz + 3) & ~3;
	constexpr int kLeftBorder14MHz_4 = kLeftBorder14MHz & ~3;
	constexpr int kRightBorder14MHz_4 = (kRightBorder14MHz + 3) & ~3;

	constexpr int kLeftBorder7MHz_8 = kLeftBorder7MHz & ~7;
	constexpr int kRightBorder7MHz_8 = (kRightBorder7MHz + 7) & ~7;
	constexpr int kLeftBorder14MHz_8 = kLeftBorder14MHz & ~7;
	constexpr int kRightBorder14MHz_8 = (kRightBorder14MHz + 7) & ~7;

	constexpr int kLeftBorder7MHz_16 = kLeftBorder7MHz & ~15;
	constexpr int kRightBorder7MHz_16 = (kRightBorder7MHz + 15) & ~15;
	constexpr int kLeftBorder14MHz_16 = kLeftBorder14MHz & ~15;
	constexpr int kRightBorder14MHz_16 = (kRightBorder14MHz + 15) & ~15;

	void GammaCorrect(uint8 *VDRESTRICT dst8, uint32 N, const uint8 *VDRESTRICT gammaTab) {
		for(uint32 i=0; i<N; ++i) {
			dst8[0] = gammaTab[dst8[0]];
			dst8[1] = gammaTab[dst8[1]];
			dst8[2] = gammaTab[dst8[2]];

			dst8 += 4;
		}
	}
}

///////////////////////////////////////////////////////////////////////////

uint32 ATPaletteCorrector::CorrectSingleColor(uint32 c, bool colorCorrectionEnabled, bool signedEncoding) const {
	union {
		uint32 c;
		uint8 v[4];
	} u = { c };

	if (colorCorrectionEnabled && static_cast<const ATArtifactingEngine *>(this)->mbEnableColorCorrection)
		static_cast<const ATArtifactingEngine *>(this)->ColorCorrect(u.v, 1);

	if (signedEncoding)
		u.c = ((u.c & 0xfefefe) >> 1) + 0x404040;

	// recompute luminance in alpha
	u.c = (u.c & 0xFFFFFF) + (((u.c & 0xFF00FF) * 0x130036 + (u.c & 0xFF00) * 0xB700 + 0x800000) & 0xFF000000);

	return u.c;
}

///////////////////////////////////////////////////////////////////////////

ATArtifactingEngine::ATArtifactingEngine()
	: mbBlendActive(false)
	, mbBlendCopy(false)
	, mbHighNTSCTablesInited(false)
	, mbHighPALTablesInited(false)
	, mbGammaIdentity(false)
{
	mArtifactingParams = ATArtifactingParams::GetDefault();
}

ATArtifactingEngine::~ATArtifactingEngine() {
}

void ATArtifactingEngine::SetColorParams(const ATColorParams& params, const vdfloat3x3 *matrix, const nsVDVecMath::vdfloat32x3 *tintColor) {
	using namespace nsVDVecMath;

	mColorParams = params;

	float lumaRamp[16];
	ATComputeLumaRamp(params.mLumaRampMode, lumaRamp);

	const float yscale = params.mContrast * params.mIntensityScale;
	const float ybias = params.mBrightness * params.mIntensityScale;

	const vdfloat2 co_r = vdfloat2x2::rotation(params.mRedShift * (nsVDMath::kfPi / 180.0f)) * vdfloat2 { +0.9563f, +0.6210f } * params.mRedScale * params.mIntensityScale;
	const vdfloat2 co_g = vdfloat2x2::rotation(params.mGrnShift * (nsVDMath::kfPi / 180.0f)) * vdfloat2 { -0.2721f, -0.6474f } * params.mGrnScale * params.mIntensityScale;
	const vdfloat2 co_b = vdfloat2x2::rotation(params.mBluShift * (nsVDMath::kfPi / 180.0f)) * vdfloat2 { -1.1070f, +1.7046f } * params.mBluScale * params.mIntensityScale;

	const float artphase = params.mArtifactHue * (nsVDMath::kfTwoPi / 360.0f);
	const vdfloat2 rot_art { cosf(artphase), sinf(artphase) };

	float artr = 64.0f * 255.0f * nsVDMath::dot(rot_art, co_r) * kSaturation / 15.0f * params.mArtifactSat;
	float artg = 64.0f * 255.0f * nsVDMath::dot(rot_art, co_g) * kSaturation / 15.0f * params.mArtifactSat;
	float artb = 64.0f * 255.0f * nsVDMath::dot(rot_art, co_b) * kSaturation / 15.0f * params.mArtifactSat;

	for(int i=-15; i<16; ++i) {
		int ar = VDRoundToInt32(artr * (float)i);
		int ag = VDRoundToInt32(artg * (float)i);
		int ab = VDRoundToInt32(artb * (float)i);

		if (ar != (sint16)ar) ar = (ar < 0) ? -0x8000 : 0x7FFF;
		if (ag != (sint16)ag) ag = (ag < 0) ? -0x8000 : 0x7FFF;
		if (ab != (sint16)ab) ab = (ab < 0) ? -0x8000 : 0x7FFF;

		mArtifactRamp[i+15][0] = ab;
		mArtifactRamp[i+15][1] = ag;
		mArtifactRamp[i+15][2] = ar;
		mArtifactRamp[i+15][3] = 0;
	}

	memset(mChromaVectors, 0, sizeof(mChromaVectors));

	vdfloat3 cvec[16];
	cvec[0] = vdfloat3 { 0, 0, 0 };

	for(int j=0; j<15; ++j) {
		float i = 0;
		float q = 0;

		if (params.mbUsePALQuirks) {
			const float step = params.mHueRange * (nsVDMath::kfTwoPi / (15.0f * 360.0f));
			const float theta = params.mHueStart * (nsVDMath::kfTwoPi / 360.0f);

			const ATPALPhaseInfo& palPhaseInfo = kATPALPhaseLookup[j];

			float angle2 = theta + step * palPhaseInfo.mEvenPhase;
			float angle3 = theta + step * palPhaseInfo.mOddPhase;
			float i2 = cosf(angle2) * palPhaseInfo.mEvenInvert;
			float q2 = sinf(angle2) * palPhaseInfo.mEvenInvert;
			float i3 = cosf(angle3) * palPhaseInfo.mOddInvert;
			float q3 = sinf(angle3) * palPhaseInfo.mOddInvert;

			i = (i2 + i3) * 0.5f;
			q = (q2 + q3) * 0.5f;
		} else {
			float theta = nsVDMath::kfTwoPi * (params.mHueStart / 360.0f + (float)j * (params.mHueRange / (15.0f * 360.0f)));
			i = cosf(theta);
			q = sinf(theta);
		}

		vdfloat2 iq { i, q };
		vdfloat3 chroma { nsVDMath::dot(co_r, iq), nsVDMath::dot(co_g, iq), nsVDMath::dot(co_b, iq) };

		chroma *= params.mSaturation;

		cvec[j+1] = chroma;

		int icr = VDRoundToInt(chroma.x * (64.0f * 255.0f));
		int icg = VDRoundToInt(chroma.y * (64.0f * 255.0f));
		int icb = VDRoundToInt(chroma.z * (64.0f * 255.0f));

		if (icr != (short)icr) icr = (icr < 0) ? -0x8000 : 0x7FFF;
		if (icg != (short)icg) icg = (icg < 0) ? -0x8000 : 0x7FFF;
		if (icb != (short)icb) icb = (icb < 0) ? -0x8000 : 0x7FFF;

		mChromaVectors[j+1][0] = icb;
		mChromaVectors[j+1][1] = icg;
		mChromaVectors[j+1][2] = icr;
	}

	for(int i=0; i<16; ++i) {
		float y = lumaRamp[i & 15] * yscale + ybias;

		mLumaRamp[i] = VDRoundToInt32(y * (64.0f * 255.0f)) + 32;
	}

	// compute gamma correction table
	const float gamma = 1.0f / params.mGammaCorrect;
	mbGammaIdentity = fabsf(params.mGammaCorrect - 1.0f) < 1e-5f;

	if (mbGammaIdentity) {
		for(int i=0; i<256; ++i)
			mGammaTable[i] = (uint8)i;
	} else {
		for(int i=0; i<256; ++i)
			mGammaTable[i] = (uint8)VDRoundToInt32(powf((float)i / 255.0f, gamma) * 255.0f);
	}

	const bool useColorTint = (tintColor != nullptr);
	vdfloat32x3 tintColorVal = tintColor ? *tintColor : vdfloat32x3::zero();

	for(int i=0; i<256; ++i) {
		int c = i >> 4;

		float y = lumaRamp[i & 15] * yscale + ybias;
		float r = cvec[c].x + y;
		float g = cvec[c].y + y;
		float b = cvec[c].z + y;
		vdfloat32x3 rgb;
		
		if (useColorTint) {
			if (c) {
				const float chroma = 0.125f;
				rgb = powf((powf(std::max<float>(y - chroma, 0.0f), 2.4f) + powf(y + chroma, 2.4f)) * 0.5f, 1.0f / 2.4f) * tintColorVal;
			} else {
				rgb = y * tintColorVal;
			}
		} else {
			rgb = vdfloat32x3::set(r, g, b);
		}

		mPalette[i] = packus8(permute<2,1,0>(rgb) * 255.0f) & 0xFFFFFF;
		mSignedPalette[i] = packus8(permute<2,1,0>(rgb) * 127.0f + 64.0f) & 0xFFFFFF;

		if (matrix) {
			mCorrectedPalette[i] = mPalette[i];
			mCorrectedSignedPalette[i] = mSignedPalette[i];
		} else {
			rgb = pow(max0(rgb), gamma);

			mCorrectedPalette[i] = packus8(permute<2,1,0>(rgb) * 255.0f) & 0xFFFFFF;
			mCorrectedSignedPalette[i] = packus8(permute<2,1,0>(rgb) * 127.0f + 64.0f) & 0xFFFFFF;
		}
	}

	mbEnableColorCorrection = matrix != nullptr;

	if (matrix) {
		mColorMatchingMatrix = *matrix;

		mColorMatchingMatrix16[0][0] = VDRoundToInt32(mColorMatchingMatrix.x.x * 16384.0f);
		mColorMatchingMatrix16[0][1] = VDRoundToInt32(mColorMatchingMatrix.x.y * 16384.0f);
		mColorMatchingMatrix16[0][2] = VDRoundToInt32(mColorMatchingMatrix.x.z * 16384.0f);
		mColorMatchingMatrix16[1][0] = VDRoundToInt32(mColorMatchingMatrix.y.x * 16384.0f);
		mColorMatchingMatrix16[1][1] = VDRoundToInt32(mColorMatchingMatrix.y.y * 16384.0f);
		mColorMatchingMatrix16[1][2] = VDRoundToInt32(mColorMatchingMatrix.y.z * 16384.0f);
		mColorMatchingMatrix16[2][0] = VDRoundToInt32(mColorMatchingMatrix.z.x * 16384.0f);
		mColorMatchingMatrix16[2][1] = VDRoundToInt32(mColorMatchingMatrix.z.y * 16384.0f);
		mColorMatchingMatrix16[2][2] = VDRoundToInt32(mColorMatchingMatrix.z.z * 16384.0f);

		// The PAL/SECAM standards specify a gamma of 2.8. However, there is apparently
		// a lot of deviation from this in the real world, where 2.2 and 2.5 are found instead.
		// 2.5 and 2.8 seem to be too extreme compared to actual screenshots of a color map
		// on an actual PAL system and display, so for now we just use 2.2.
		const float nativeGamma = 2.2f;

		for(int i=0; i<256; ++i) {
			float x = (float)i / 255.0f;
			float y = powf(x, nativeGamma);

			mCorrectLinearTable[i] = (sint16)floor(0.5f + y * 8191.0f);
		}

		if (params.mColorMatchingMode == ATColorMatchingMode::AdobeRGB) {
			for(int i=0; i<1024; ++i) {
				float x = (float)i / 1023.0f;
				float y = (x < 0) ? 0.0f : powf(x, 1.0f / 2.2f);

				y = powf(y, gamma);

				mCorrectGammaTable[i] = (uint8)(y * 255.0f + 0.5f);
			}
		} else {
			for(int i=0; i<1024; ++i) {
				float x = (float)i / 1023.0f;
				float y = (x < 0.0031308f) ? x * 12.92f : 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;

				y = powf(y, gamma);

				mCorrectGammaTable[i] = (uint8)(y * 255.0f + 0.5f);
			}
		}

		ColorCorrect((uint8 *)mCorrectedPalette, 256);

		// correct the signed palette manually
		for(int i=0; i<256; ++i) {
			uint32 c = mCorrectedSignedPalette[i];
			float r = (float)((c >> 16) & 0xFF) - 64.0f;
			float g = (float)((c >>  8) & 0xFF) - 64.0f;
			float b = (float)((c >>  0) & 0xFF) - 64.0f;

			if (r < 0.0f) r = 0.0f;
			if (g < 0.0f) g = 0.0f;
			if (b < 0.0f) b = 0.0f;

			r = powf(r, nativeGamma);
			g = powf(g, nativeGamma);
			b = powf(b, nativeGamma);

			vdfloat3 rgb{r, g, b};
			vdfloat3 rgb2 = rgb * (*matrix);

			r = rgb2.x;
			g = rgb2.y;
			b = rgb2.z;

			if (r < 0.0f) r = 0.0f;
			if (g < 0.0f) g = 0.0f;
			if (b < 0.0f) b = 0.0f;

			r = powf(r, 1.0f / nativeGamma) + 64.0f;
			g = powf(g, 1.0f / nativeGamma) + 64.0f;
			b = powf(b, 1.0f / nativeGamma) + 64.0f;

			if (r > 255.0f) r = 255.0f;
			if (g > 255.0f) g = 255.0f;
			if (b > 255.0f) b = 255.0f;

			uint32 ir = (uint32)(sint32)(r + 0.5f);
			uint32 ig = (uint32)(sint32)(g + 0.5f);
			uint32 ib = (uint32)(sint32)(b + 0.5f);

			mCorrectedSignedPalette[i] = (ir << 16) + (ig << 8) + ib;
		}
	}

	mbTintColorEnabled = (tintColor != nullptr);

	if (tintColor) {
		mTintColor.x = tintColor->x();
		mTintColor.y = tintColor->y();
		mTintColor.z = tintColor->z();

		vdfloat32x3 bgrTint = permute<2,1,0>(*tintColor);
		vdfloat32x3 c = vdfloat32x3::zero();

		for(int i=0; i<256; ++i) {
			mMonoTable[i] = packus8(c);
			c += bgrTint;
		}
	}

	mbHighNTSCTablesInited = false;
	mbHighPALTablesInited = false;
	mbActiveTablesInited = false;
}

void ATArtifactingEngine::SetArtifactingParams(const ATArtifactingParams& params) {
	mArtifactingParams = params;

	mbHighNTSCTablesInited = false;
	mbHighPALTablesInited = false;
}

void ATArtifactingEngine::GetNTSCArtifactColors(uint32 c[2]) const {
	for(int i=0; i<2; ++i) {
		int art = i ? 0 : 30;
		int y = mLumaRamp[7];

		int cr = mArtifactRamp[art][2] + y;
		int cg = mArtifactRamp[art][1] + y;
		int cb = mArtifactRamp[art][0] + y;

		cr >>= 6;
		cg >>= 6;
		cb >>= 6;

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

		if (!mbGammaIdentity && !mbEnableColorCorrection) {
			cr = mGammaTable[cr];
			cg = mGammaTable[cg];
			cb = mGammaTable[cb];
		}

		c[i] = cb + (cg << 8) + (cr << 16);
	}

	if (mbEnableColorCorrection)
		ColorCorrect((uint8 *)c, 2);

}

void ATArtifactingEngine::DumpHighArtifactingFilters(ATConsoleOutput& output) {
	if (mbHighPALTablesInited)
		RecomputePALTables(&output);
	else if (mbHighNTSCTablesInited)
		RecomputeNTSCTables(&output);
	else
		output <<= "High artifacting tables not initialized.";
}

void ATArtifactingEngine::SuspendFrame() {
	mbSavedPAL = mbPAL;
	mbSavedChromaArtifacts = mbChromaArtifacts;
	mbSavedChromaArtifactsHi = mbChromaArtifactsHi;
	mbSavedBypassOutputCorrection = mbBypassOutputCorrection;
	mbSavedBlendActive = mbBlendActive;
	mbSavedBlendCopy = mbBlendCopy;
	mbSavedBlendLinear = mbBlendLinear;
	mbSavedExpandedRangeInput = mbExpandedRangeInput;
	mbSavedExpandedRangeOutput = mbExpandedRangeOutput;
}

void ATArtifactingEngine::ResumeFrame() {
	mbPAL = mbSavedPAL;
	mbChromaArtifacts = mbSavedChromaArtifacts;
	mbChromaArtifactsHi = mbSavedChromaArtifactsHi;
	mbBypassOutputCorrection = mbSavedBypassOutputCorrection;
	mbBlendActive = mbSavedBlendActive;
	mbBlendCopy = mbSavedBlendCopy;
	mbBlendLinear = mbSavedBlendLinear;
	mbExpandedRangeInput = mbSavedExpandedRangeInput;
	mbExpandedRangeOutput = mbSavedExpandedRangeOutput;
}

void ATArtifactingEngine::BeginFrame(bool pal, bool chromaArtifacts, bool chromaArtifactHi, bool blendIn, bool blendOut, bool blendLinear, bool bypassOutputCorrection, bool extendedRangeInput, bool extendedRangeOutput) {
	// we don't support expanding output in the artifacting engine
	VDASSERT(!(!extendedRangeInput && extendedRangeOutput));

	mbPAL = pal;
	mbChromaArtifacts = chromaArtifacts;
	mbChromaArtifactsHi = chromaArtifactHi;
	mbBypassOutputCorrection = bypassOutputCorrection;
	mbExpandedRangeInput = extendedRangeInput;
	mbExpandedRangeOutput = extendedRangeOutput;

	if (!mbActiveTablesInited || mbActiveTablesSigned != extendedRangeOutput)
		RecomputeActiveTables(extendedRangeOutput);

	if (chromaArtifactHi) {
		if (pal) {
			if (!mbHighPALTablesInited || mbHighTablesSigned != extendedRangeOutput)
				RecomputePALTables(nullptr);
		} else {
			if (!mbHighNTSCTablesInited || mbHighTablesSigned != extendedRangeOutput)
				RecomputeNTSCTables(nullptr);
		}
	}

	if (pal && chromaArtifacts) {
		if (chromaArtifactHi) {
#if defined(VD_CPU_AMD64)
			memset(mPALDelayLineUV, 0, sizeof mPALDelayLineUV);
#else
#if defined(VD_CPU_X86)
			if (SSE2_enabled) {
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

	mbBlendCopy = !blendIn;
	mbBlendActive = blendOut;
	mbBlendLinear = blendLinear;
}

void ATArtifactingEngine::Artifact8(uint32 y, uint32 dst[N], const uint8 src[N], bool scanlineHasHiRes, bool temporaryUpdate, bool includeHBlank) {
	if (!mbChromaArtifacts)
		BlitNoArtifacts(dst, src, scanlineHasHiRes);
	else if (mbPAL) {
		if (mbChromaArtifactsHi)
			ArtifactPALHi(dst, src, scanlineHasHiRes, (y & 1) != 0);
		else
			ArtifactPAL8(dst, src);
	} else {
		if (mbChromaArtifactsHi)
			ArtifactNTSCHi(dst, src, scanlineHasHiRes, includeHBlank);
		else
			ArtifactNTSC(dst, src, scanlineHasHiRes, includeHBlank);
	}

	if (mbBlendActive && y < M) {
		uint32 *blendDst = mbChromaArtifactsHi ? mPrevFrame14MHz[y] : mPrevFrame7MHz[y];
		uint32 n = mbChromaArtifactsHi ? N*2 : N;

		if (!includeHBlank) {
			if (mbChromaArtifactsHi) {
				blendDst += kLeftBorder14MHz_4;
				dst += kLeftBorder14MHz_4;
				n = kRightBorder14MHz_4 - kLeftBorder14MHz_4;
			} else {
				blendDst += kLeftBorder7MHz_4;
				dst += kLeftBorder7MHz_4;
				n = kRightBorder7MHz_4 - kLeftBorder7MHz_4;
			}
		}

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

// Artifacting entry point for 32-bit RGB outputs.
//
// If PAL chroma artifacts are enabled, then the input must be YRGB instead of XRGB, where Y is luminance. This
// is used to speed up the chroma blending calculations.
//
void ATArtifactingEngine::Artifact32(uint32 y, uint32 *dst, uint32 width, bool temporaryUpdate, bool includeHBlank) {
	if (mbPAL && mbChromaArtifacts)
		ArtifactPAL32(dst, width);
	else if (mbExpandedRangeInput && !mbExpandedRangeOutput)
		ArtifactCompressRange(dst, width);

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

	if (!mbBypassOutputCorrection) {
		if (mbEnableColorCorrection)
			ColorCorrect((uint8 *)dst, width);
		if (!mbGammaIdentity)
			GammaCorrect((uint8 *)dst, width, mGammaTable);
	}
}

void ATArtifactingEngine::InterpolateScanlines(uint32 *dst, const uint32 *src1, const uint32 *src2, uint32 n) {
#if VD_CPU_X86 || VD_CPU_X64
	if (SSE2_enabled) {
		ATArtifactBlendScanlines_SSE2(dst, src1, src2, n, mArtifactingParams.mScanlineIntensity);
		return;
	}
#elif VD_CPU_ARM64
	ATArtifactBlendScanlines_NEON(dst, src1, src2, n, mArtifactingParams.mScanlineIntensity);
	return;
#endif

	for(uint32 i=0; i<n; ++i) {
		uint32 prev = src1[i];
		uint32 next = src2[i];
		uint32 r = (prev | next) - (((prev ^ next) & 0xfefefe) >> 1);

		r -= (r & 0xfcfcfc) >> 2;
		dst[i] = r;
	}
}

void ATArtifactingEngine::ArtifactPAL8(uint32 dst[N], const uint8 src[N]) {
	const uint32 *VDRESTRICT palette = mbBypassOutputCorrection ? mPalette : mbExpandedRangeOutput ? mCorrectedSignedPalette : mCorrectedPalette;

	for(int i=0; i<N; ++i) {
		uint8 prev = mPALDelayLine[i];
		uint8 next = src[i];
		uint32 prevColor = palette[(prev & 0xf0) + (next & 0x0f)];
		uint32 nextColor = palette[next];

		dst[i] = (prevColor | nextColor) - (((prevColor ^ nextColor) & 0xfefefe) >> 1);
	}

	memcpy(mPALDelayLine, src, sizeof mPALDelayLine);
}

void ATArtifactingEngine::ArtifactPAL32(uint32 *dst, uint32 width) {
	bool compressOutput = mbExpandedRangeInput && !mbExpandedRangeOutput;

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	if (SSE2_enabled) {
		ATArtifactPAL32_SSE2(dst, mPALDelayLine32, width, compressOutput);
		return;
	}
#endif

	ATArtifactPAL32(dst, mPALDelayLine32, width, compressOutput);
}

void ATArtifactingEngine::ArtifactCompressRange(uint32 *dst, uint32 width) {
#if defined(VD_COMPILER_MSVC) && (defined(VD_CPU_X86) || defined(VD_CPU_X64))
	if (SSE2_enabled) {
		uint32 preAlign = (((uintptr)16 - (uintptr)dst) & 15) >> 2;

		if (width > preAlign) {
			ArtifactCompressRange_Scalar(dst, preAlign);
			dst += preAlign;
			width -= preAlign;

			if (width >= 4)
				ArtifactCompressRange_SSE2(dst, width);

			uint32 n2 = width & ~3;
			dst += n2;
			width &= 3;
		}
	}
#endif

	return ArtifactCompressRange_Scalar(dst, width);
}

void ATArtifactingEngine::ArtifactCompressRange_Scalar(uint32 *dst, uint32 width) {

	while(width--) {
		uint32 c = *dst;
		uint32 r = (c >> 16) & 0xFF;
		uint32 g = (c >>  8) & 0xFF;
		uint32 b = (c >>  0) & 0xFF;

		r = (r < 0x40) ? 0 : (r >= 0xC0) ? 0xFF : (r - 0x40)*2;
		g = (g < 0x40) ? 0 : (g >= 0xC0) ? 0xFF : (g - 0x40)*2;
		b = (b < 0x40) ? 0 : (b >= 0xC0) ? 0xFF : (b - 0x40)*2;

		*dst = (c & 0xFF000000) + (r << 16) + (g << 8) + b;
		++dst;
	}
}

void ATArtifactingEngine::ArtifactNTSC(uint32 dst[N], const uint8 src[N], bool scanlineHasHiRes, bool includeHBlank) {
	if (!scanlineHasHiRes) {
		BlitNoArtifacts(dst, src, false);
		return;
	}

#if defined(VD_COMPILER_MSVC) && (defined(VD_CPU_X86) || defined(VD_CPU_X64))
	if (SSE2_enabled) {
		if (!ArtifactNTSC_SSE2(dst, src, N))
			BlitNoArtifacts(dst, src, true);

		return;
	}
#endif

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
		BlitNoArtifacts(dst, src, true);
		return;
	}

	// This has no physical basis -- it just looks OK.
	uint32 *dst2 = dst;

	if (mbEnableColorCorrection || mbGammaIdentity || mbBypassOutputCorrection || mbExpandedRangeOutput) {
		for(int x=0; x<N; ++x) {
			uint8 p = src[x];
			int art = inv[x]; 

			if (!art) {
				*dst2++ = mActivePalette[p];
			} else {
				int c = p >> 4;

				int cr = mActiveChromaVectors[c][2];
				int cg = mActiveChromaVectors[c][1];
				int cb = mActiveChromaVectors[c][0];
				int y = mActiveLumaRamp[luma2[x]];

				cr += mActiveArtifactRamp[art+15][2] + y;
				cg += mActiveArtifactRamp[art+15][1] + y;
				cb += mActiveArtifactRamp[art+15][0] + y;

				cr >>= 6;
				cg >>= 6;
				cb >>= 6;

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

				*dst2++ = cb + (cg << 8) + (cr << 16);
			}
		}

		if (mbEnableColorCorrection && !mbBypassOutputCorrection)
			ColorCorrect((uint8 *)dst, N);
	} else {
		// If we are going down this path we can't be using the signed palette, so we're OK not
		// using the active arrays.
		for(int x=0; x<N; ++x) {
			uint8 p = src[x];
			int art = inv[x]; 

			if (!art) {
				*dst2++ = mCorrectedPalette[p];
			} else {
				int c = p >> 4;

				int cr = mChromaVectors[c][2];
				int cg = mChromaVectors[c][1];
				int cb = mChromaVectors[c][0];
				int y = mLumaRamp[luma2[x]];

				cr += mArtifactRamp[art+15][0] + y;
				cg += mArtifactRamp[art+15][1] + y;
				cb += mArtifactRamp[art+15][2] + y;

				cr >>= 6;
				cg >>= 6;
				cb >>= 6;

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

				*dst2++ = cb + (cg << 8) + (cr << 16);
			}
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

	void rotate(float& xr, float& yr, float angle) {
		const float sn = sinf(angle);
		const float cs = cosf(angle);
		float x0 = xr;
		float y0 = yr;

		xr = x0*cs + y0*sn;
		yr = -x0*sn + y0*cs;
	}

	[[maybe_unused]] void accum(uint32 *VDRESTRICT dst, const uint32 (*VDRESTRICT table)[2][12], const uint8 *VDRESTRICT src, uint32 count) {
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

	[[maybe_unused]] void accum_twin(uint32 *VDRESTRICT dst, const uint32 (*VDRESTRICT table)[12], const uint8 *VDRESTRICT src, uint32 count) {
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
			int r0 = ((int)(rp & 0xffff) - 0x7ff8) >> 4;
			int g0 = ((int)(gp & 0xffff) - 0x7ff8) >> 4;
			int b0 = ((int)(bp & 0xffff) - 0x7ff8) >> 4;
			int r1 = ((int)(rp >> 16) - 0x7ff8) >> 4;
			int g1 = ((int)(gp >> 16) - 0x7ff8) >> 4;
			int b1 = ((int)(bp >> 16) - 0x7ff8) >> 4;

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

void ATArtifactColorCorrect_Scalar(uint8 *VDRESTRICT dst8, uint32 N, const sint16 linearTab[256], const uint8 gammaTab[1024], const sint16 matrix16[3][3]) {
	for(uint32 i=0; i<N; ++i) {
		// convert NTSC gamma to linear (2.2)
		sint32 r = linearTab[dst8[2]];
		sint32 g = linearTab[dst8[1]];
		sint32 b = linearTab[dst8[0]];

		// convert to new color space
		sint32 r2
			= r * matrix16[0][0]
			+ g * matrix16[1][0]
			+ b * matrix16[2][0];

		sint32 g2
			= r * matrix16[0][1]
			+ g * matrix16[1][1]
			+ b * matrix16[2][1];

		sint32 b2
			= r * matrix16[0][2]
			+ g * matrix16[1][2]
			+ b * matrix16[2][2];

		r2 >>= 17;
		g2 >>= 17;
		b2 >>= 17;

		if (r2 < 0) r2 = 0;
		if (g2 < 0) g2 = 0;
		if (b2 < 0) b2 = 0;
		if (r2 > 1023) r2 = 1023;
		if (g2 > 1023) g2 = 1023;
		if (b2 > 1023) b2 = 1023;

		dst8[2] = gammaTab[r2];
		dst8[1] = gammaTab[g2];
		dst8[0] = gammaTab[b2];
		dst8 += 4;
	}
}

#if VD_CPU_X86 || VD_CPU_X64
void ATArtifactColorCorrect_SSE2(uint8 *VDRESTRICT dst8, uint32 N, const sint16 linearTab[256], const uint8 gammaTab[1024], const sint16 matrix16[3][3]) {
	__m128i m0 = _mm_loadl_epi64((const __m128i *)matrix16[0]);
	__m128i m1 = _mm_loadl_epi64((const __m128i *)matrix16[1]);
	__m128i m2 = _mm_loadl_epi64((const __m128i *)matrix16[2]);
	__m128i zero = _mm_setzero_si128();
	__m128i limit = _mm_set_epi16(0, 0, 0, 0, 0, 1023, 1023, 1023);

	for(uint32 i=0; i<N; ++i) {
		// convert NTSC gamma to linear (2.2)
		__m128i r = _mm_cvtsi32_si128((uint16)linearTab[dst8[2]]);
		__m128i g = _mm_cvtsi32_si128((uint16)linearTab[dst8[1]]);
		__m128i b = _mm_cvtsi32_si128((uint16)linearTab[dst8[0]]);

		// convert to new color space
		__m128i rgb = _mm_mulhi_epi16(_mm_shufflelo_epi16(r, 0), m0);
		rgb = _mm_add_epi16(rgb, _mm_mulhi_epi16(_mm_shufflelo_epi16(g, 0), m1));
		rgb = _mm_add_epi16(rgb, _mm_mulhi_epi16(_mm_shufflelo_epi16(b, 0), m2));

		__m128i indices = _mm_max_epi16(zero, _mm_min_epi16(limit, _mm_srai_epi16(rgb, 1)));

		dst8[2] = gammaTab[(uint32)_mm_extract_epi16(indices, 0)];
		dst8[1] = gammaTab[(uint32)_mm_extract_epi16(indices, 1)];
		dst8[0] = gammaTab[(uint32)_mm_extract_epi16(indices, 2)];
		dst8 += 4;
	}
}
#endif

void ATArtifactingEngine::ColorCorrect(uint8 *VDRESTRICT dst8, uint32 n) const {
#if VD_CPU_X86 || VD_CPU_X64
	if (SSE2_enabled)
		ATArtifactColorCorrect_SSE2(dst8, n, mCorrectLinearTable, mCorrectGammaTable, mColorMatchingMatrix16);
	else
#endif
		ATArtifactColorCorrect_Scalar(dst8, n, mCorrectLinearTable, mCorrectGammaTable, mColorMatchingMatrix16);
}

void ATArtifactingEngine::ArtifactNTSCHi(uint32 dst[N*2], const uint8 src[N], bool scanlineHasHiRes, bool includeHBlank) {
	// We are using a 21 tap filter, so we're going to need arrays of N*2+20 (since we are
	// transforming 7MHz input to 14MHz output). However, we hold two elements in each int,
	// so we actually only need N+10 elements, which we round up to N+16. We need 8-byte
	// alignment for MMX.
	VDALIGN(16) uint32 rout[N+16];
	VDALIGN(16) uint32 gout[N+16];
	VDALIGN(16) uint32 bout[N+16];

#if defined(VD_CPU_ARM64)
	if (scanlineHasHiRes) {
		ATArtifactNTSCAccum_NEON(rout+2, m4x.mPalToR, src, N);
		ATArtifactNTSCAccum_NEON(gout+2, m4x.mPalToG, src, N);
		ATArtifactNTSCAccum_NEON(bout+2, m4x.mPalToB, src, N);
	} else {
		ATArtifactNTSCAccumTwin_NEON(rout+2, m4x.mPalToRTwin, src, N);
		ATArtifactNTSCAccumTwin_NEON(gout+2, m4x.mPalToGTwin, src, N);
		ATArtifactNTSCAccumTwin_NEON(bout+2, m4x.mPalToBTwin, src, N);
	}
#elif defined(VD_CPU_AMD64)
	if (scanlineHasHiRes) {
		ATArtifactNTSCAccum_SSE2(rout+2, m4x.mPalToR, src, N);
		ATArtifactNTSCAccum_SSE2(gout+2, m4x.mPalToG, src, N);
		ATArtifactNTSCAccum_SSE2(bout+2, m4x.mPalToB, src, N);
	} else {
		ATArtifactNTSCAccumTwin_SSE2(rout+2, m4x.mPalToRTwin, src, N);
		ATArtifactNTSCAccumTwin_SSE2(gout+2, m4x.mPalToGTwin, src, N);
		ATArtifactNTSCAccumTwin_SSE2(bout+2, m4x.mPalToBTwin, src, N);
	}
#else

#if defined(VD_COMPILER_MSVC) && defined(VD_CPU_X86)
	if (SSE2_enabled) {
		if (scanlineHasHiRes) {
			ATArtifactNTSCAccum_SSE2(rout+2, m4x.mPalToR, src, N);
			ATArtifactNTSCAccum_SSE2(gout+2, m4x.mPalToG, src, N);
			ATArtifactNTSCAccum_SSE2(bout+2, m4x.mPalToB, src, N);
		} else {
			ATArtifactNTSCAccumTwin_SSE2(rout+2, m4x.mPalToRTwin, src, N);
			ATArtifactNTSCAccumTwin_SSE2(gout+2, m4x.mPalToGTwin, src, N);
			ATArtifactNTSCAccumTwin_SSE2(bout+2, m4x.mPalToBTwin, src, N);
		}
	} else
#endif
	{
		for(int i=0; i<N+16; ++i)
			rout[i] = 0x80008000;

		if (scanlineHasHiRes)
			accum(rout, m2x.mPalToR, src, N);
		else
			accum_twin(rout, m2x.mPalToRTwin, src, N);

		for(int i=0; i<N+16; ++i)
			gout[i] = 0x80008000;

		if (scanlineHasHiRes)
			accum(gout, m2x.mPalToG, src, N);
		else
			accum_twin(gout, m2x.mPalToGTwin, src, N);

		for(int i=0; i<N+16; ++i)
			bout[i] = 0x80008000;

		if (scanlineHasHiRes)
			accum(bout, m2x.mPalToB, src, N);
		else
			accum_twin(bout, m2x.mPalToBTwin, src, N);
	}
#endif

	// downconvert+interleave RGB channels and do post-processing
	const int xdfinal = includeHBlank ? 0 : kLeftBorder14MHz_16;
	const int xfinal = includeHBlank ? 0 : kLeftBorder7MHz_8/2;
	const int nfinal = includeHBlank ? N : ((kRightBorder7MHz - kLeftBorder7MHz_8) + 7) & ~7;

#if VD_CPU_ARM64
	ATArtifactNTSCFinal_NEON(dst + xdfinal, rout+4+xfinal, gout+4+xfinal, bout+4+xfinal, nfinal);
#else
#if VD_CPU_X86 || VD_CPU_X64
	if (SSE2_enabled)
		ATArtifactNTSCFinal_SSE2(dst + xdfinal, rout+4+xfinal, gout+4+xfinal, bout+4+xfinal, nfinal);
	else
#endif
		final(dst + xdfinal, rout+4+xfinal, gout+4+xfinal, bout+4+xfinal, nfinal);
#endif

	if (!mbBypassOutputCorrection) {
		const int xpost = includeHBlank ? 0 : kLeftBorder14MHz;
		const int npost = includeHBlank ? N*2 : kRightBorder14MHz - kLeftBorder14MHz;

		if (mbEnableColorCorrection)
			ColorCorrect((uint8 *)(dst + xpost), npost);
		else if (!mbGammaIdentity)
			GammaCorrect((uint8 *)(dst + xpost), npost, mGammaTable);
	}
}

void ATArtifactingEngine::ArtifactPALHi(uint32 dst[N*2], const uint8 src0[N], bool scanlineHasHiRes, bool oddLine) {
	// encode to YUV
	VDALIGN(16) uint32 ybuf[32 + N];
	VDALIGN(16) uint32 ubuf[32 + N];
	VDALIGN(16) uint32 vbuf[32 + N];

	uint32 *const ulbuf = mPALDelayLineUV[0];
	uint32 *const vlbuf = mPALDelayLineUV[1];

	// Shift the source data by 2 hires pixels to center the kernels; this is
	// needed since we can only grossly align the YUV arrays by 128-bit amounts
	// (8 hires pixels). There is a matching 90d phase shift in the kernel
	// computation code.
	uint8 src[N+16];
	memset(src, 0, 2);
	memcpy(src + 2, src0, N);
	memset(src + 2 + N, 0, 14);

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
	if (SSE2_enabled) {
		// luma routine writes N+8 elements (requires N multiple of 8)
		// chroma routine writes N+8 elements (requires N multiple of 16)

		if (scanlineHasHiRes) {
			ATArtifactPALLuma_SSE2(ybuf, src, N+16, &mPal8x.mPalToY[oddLine][0][0][0]);

			if (!mbTintColorEnabled) {
				ATArtifactPALChroma_SSE2(ubuf, src, N+16, &mPal8x.mPalToU[oddLine][0][0][0]);
				ATArtifactPALChroma_SSE2(vbuf, src, N+16, &mPal8x.mPalToV[oddLine][0][0][0]);
			}
		} else {
			ATArtifactPALLumaTwin_SSE2(ybuf, src, N+16, &mPal8x.mPalToYTwin[oddLine][0][0][0]);

			if (!mbTintColorEnabled) {
				ATArtifactPALChromaTwin_SSE2(ubuf, src, N+16, &mPal8x.mPalToUTwin[oddLine][0][0][0]);
				ATArtifactPALChromaTwin_SSE2(vbuf, src, N+16, &mPal8x.mPalToVTwin[oddLine][0][0][0]);
			}
		}

		if (mbTintColorEnabled)
			ATArtifactPALFinalMono_SSE2(dst, ybuf + 4, N, mTintColor);
		else
			ATArtifactPALFinal_SSE2(dst, ybuf + 4, ubuf + 4, vbuf + 4, ulbuf, vlbuf, N);
	} else 
#endif
	{
		VDMemset32(ubuf, 0x20002000, sizeof(ubuf)/sizeof(ubuf[0]));
		VDMemset32(vbuf, 0x20002000, sizeof(vbuf)/sizeof(vbuf[0]));

		ATArtifactPALLuma(ybuf, src, N+16, &mPal2x.mPalToY[oddLine][0][0][0]);

		if (!mbTintColorEnabled) {
			ATArtifactPALChroma(ubuf, src, N+16, &mPal2x.mPalToU[oddLine][0][0][0]);
			ATArtifactPALChroma(vbuf, src, N+16, &mPal2x.mPalToV[oddLine][0][0][0]);
		}

		if (mbTintColorEnabled)
			ATArtifactPALFinalMono(dst, ybuf + 4, N+16, mMonoTable);
		else
			ATArtifactPALFinal(dst, ybuf + 4, ubuf + 4, vbuf + 4, ulbuf, vlbuf, N);
	}

	if (!mbBypassOutputCorrection) {
		if (mbEnableColorCorrection)
			ColorCorrect((uint8 *)dst, N*2);
		else if (!mbGammaIdentity)
			GammaCorrect((uint8 *)dst, N*2, mGammaTable);
	}
}

void ATArtifactingEngine::BlitNoArtifacts(uint32 dst[N], const uint8 src[N], bool scanlineHasHiRes) {
	static_assert((N & 1) == 0);

	const uint32 *VDRESTRICT palette = mbBypassOutputCorrection ? mActivePalette : mbExpandedRangeOutput ? mCorrectedSignedPalette : mCorrectedPalette;

	if (scanlineHasHiRes) {
		for(size_t x=0; x<N; ++x)
			dst[x] = palette[src[x]];
	} else {
		for(size_t x=0; x<N; x += 2) {
			const uint32 c = palette[src[x]];
			dst[x+0] = c;
			dst[x+1] = c;
		}
	}
}

template<typename T>
void ATArtifactBlendExchange_Reference(uint32 *VDRESTRICT dst, T *VDRESTRICT src, uint32 n) {
	for(uint32 x=0; x<n; ++x) {
		const uint32 a = dst[x];
		const uint32 b = src[x];

		if constexpr(!std::is_const_v<T>) {
			src[x] = a;
		}

		dst[x] = (a|b) - (((a^b) >> 1) & 0x7f7f7f7f);
	}
}

template<bool T_ExtendedRange, typename T>
void ATArtifactBlendExchangeLinear_Reference(uint32 *VDRESTRICT dst, T *VDRESTRICT src, uint32 n) {
	for(uint32 x=0; x<n; ++x) {
		union {
			uint32 p;
			uint8 b[4];
		} a = { dst[x] }, b = { src[x] };

		if constexpr(T_ExtendedRange) {
			if (a.b[0] >= 0x40) a.b[0] -= 0x40; else a.b[0] = 0;
			if (a.b[1] >= 0x40) a.b[1] -= 0x40; else a.b[1] = 0;
			if (a.b[2] >= 0x40) a.b[2] -= 0x40; else a.b[2] = 0;
		}

		if constexpr(!std::is_const_v<T>) {
			src[x] = a.p;
		}

		a.b[0] = (uint8)(0.5f + sqrtf(((float)a.b[0]*(float)a.b[0] + (float)b.b[0]*(float)b.b[0]) * 0.5f));
		a.b[1] = (uint8)(0.5f + sqrtf(((float)a.b[1]*(float)a.b[1] + (float)b.b[1]*(float)b.b[1]) * 0.5f));
		a.b[2] = (uint8)(0.5f + sqrtf(((float)a.b[2]*(float)a.b[2] + (float)b.b[2]*(float)b.b[2]) * 0.5f));

		if constexpr(T_ExtendedRange) {
			if (a.b[0] >= 0xC0) a.b[0] = 0xFF; else a.b[0] += 0x40;
			if (a.b[1] >= 0xC0) a.b[1] = 0xFF; else a.b[1] += 0x40;
			if (a.b[2] >= 0xC0) a.b[2] = 0xFF; else a.b[2] += 0x40;
		}

		dst[x] = a.p;
	}
}

void ATArtifactingEngine::Blend(uint32 *VDRESTRICT dst, const uint32 *VDRESTRICT src, uint32 n) {
#if VD_CPU_ARM64
	if (!(n & 3)) {
		if (mbBlendLinear)
			ATArtifactBlendLinear_NEON(dst, src, n, mbExpandedRangeOutput);
		else
			ATArtifactBlend_NEON(dst, src, n);
		return;
	}
#endif
#if VD_CPU_X86 || VD_CPU_X64
	if (SSE2_enabled && !(((uintptr)dst | (uintptr)src) & 15) && !(n & 3)) {
		if (mbBlendLinear)
			ATArtifactBlendLinear_SSE2(dst, src, n, mbExpandedRangeOutput);
		else
			ATArtifactBlend_SSE2(dst, src, n);
		return;
	}
#endif

	if (mbBlendLinear) {
		if (mbExpandedRangeOutput)
			ATArtifactBlendExchangeLinear_Reference<true>(dst, src, n);
		else
			ATArtifactBlendExchangeLinear_Reference<false>(dst, src, n);
	} else
		ATArtifactBlendExchange_Reference(dst, src, n);
}

void ATArtifactingEngine::BlendExchange(uint32 *VDRESTRICT dst, uint32 *VDRESTRICT blendDst, uint32 n) {
#if VD_CPU_ARM64
	if (!(n & 3)) {
		if (mbBlendLinear)
			ATArtifactBlendExchangeLinear_NEON(dst, blendDst, n, mbExpandedRangeOutput);
		else
			ATArtifactBlendExchange_NEON(dst, blendDst, n);
		return;
	}
#endif
#if VD_CPU_X86 || VD_CPU_X64
	if (SSE2_enabled && !(((uintptr)dst | (uintptr)blendDst) & 15) && !(n & 3)) {
		if (mbBlendLinear)
			ATArtifactBlendExchangeLinear_SSE2(dst, blendDst, n, mbExpandedRangeOutput);
		else
			ATArtifactBlendExchange_SSE2(dst, blendDst, n);

		return;
	}
#endif

	if (mbBlendLinear) {
		if (mbExpandedRangeOutput)
			ATArtifactBlendExchangeLinear_Reference<true>(dst, blendDst, n);
		else
			ATArtifactBlendExchangeLinear_Reference<false>(dst, blendDst, n);
	} else
		ATArtifactBlendExchange_Reference(dst, blendDst, n);
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
            b1 = a1;
            b2 = inv_a0;
		}
	};
}

void ATArtifactingEngine::RecomputeActiveTables(bool signedOutput) {
	mbActiveTablesInited = true;
	mbActiveTablesSigned = signedOutput;

	if (signedOutput) {
		memcpy(mActivePalette, mSignedPalette, sizeof mActivePalette);

		for(int i=0; i<16; ++i) {
			mActiveChromaVectors[i][0] = (mChromaVectors[i][0] + 1) >> 1;
			mActiveChromaVectors[i][1] = (mChromaVectors[i][1] + 1) >> 1;
			mActiveChromaVectors[i][2] = (mChromaVectors[i][2] + 1) >> 1;
			mActiveChromaVectors[i][3] = (mChromaVectors[i][3] + 1) >> 1;
		}

		for(int i=0; i<16; ++i)
			mActiveLumaRamp[i] = (mLumaRamp[i] >> 1) + (0x40 << 6);

		for(int i=0; i<31; ++i) {
			mActiveArtifactRamp[i][0] = (mArtifactRamp[i][0] + 1) >> 1;
			mActiveArtifactRamp[i][1] = (mArtifactRamp[i][1] + 1) >> 1;
			mActiveArtifactRamp[i][2] = (mArtifactRamp[i][2] + 1) >> 1;
			mActiveArtifactRamp[i][3] = (mArtifactRamp[i][3] + 1) >> 1;
		}

	} else {
		memcpy(mActivePalette, mPalette, sizeof mActivePalette);
		memcpy(mActiveChromaVectors, mChromaVectors, sizeof mActiveChromaVectors);
		memcpy(mActiveLumaRamp, mLumaRamp, sizeof mActiveLumaRamp);
		memcpy(mActiveArtifactRamp, mArtifactRamp, sizeof mActiveArtifactRamp);
	}
}

void ATArtifactingEngine::RecomputeNTSCTables(ATConsoleOutput *debugOut) {
	const ATColorParams& params = mColorParams;
	const bool signedOutput = mbExpandedRangeOutput;

	mbHighNTSCTablesInited = true;
	mbHighPALTablesInited = false;
	mbHighTablesSigned = signedOutput;

	vdfloat3 y_to_rgb[16][2][24] {};
	vdfloat3 chroma_to_rgb[16][2][24] {};

	// NTSC signal parameters:
	//
	// Decoding matrix we use:
	//
	//	R-Y =  0.956*I + 0.620*Q
	//	G-Y = -0.272*I - 0.647*Q
	//	B-Y = -1.108*I + 1.705*Q
	//
	// Blank is at 0 IRE, black at 7.5 IRE, white at 100 IRE.
	// Color burst signal is +/-20 IRE.
	//
	// For 100% bars:
	//	white		100 IRE
	//	yellow		89.4 +/- 41.4 IRE (chroma 82.7 IRQ p-p)
	//	cyan		72.3 +/- 58.5 IRE
	//	green		61.8 +/- 54.7 IRE
	//	magenta		45.7 +/- 54.7 IRE
	//	red			35.1 +/- 58.5 IRE
	//	blue		18.0 +/- 41.4 IRE
	//
	// {Matlab/Scilab solver: YIQ = inv([1 0.956 0.620; 1 -0.272 -0.647; 1 -1.108 1.705])*[R G B]'; disp(YIQ); disp(YIQ(1)*92.5+7.5); disp(norm(YIQ(2:3))*100*0.925)}
	//
	// Note that the chrominance signal amplitude is also reduced when
	// adjusting for the 7.5 IRE pedestal.
	//
	// However, since the computer outputs the same amplitude for the colorburst as
	// for regular color, the saturation is reduced. Take 100% yellow, which
	// has a IQ amplitude of 0.447 raw and 41.4 IRE; the computer would produce
	// the equivalent of 20 IRE after the color AGC kicked in, giving an equivalent
	// raw IQ amplitude of 0.447 * 20 / 41.4 = 21.6%. In theory this is invariant
	// to the actual chrominance signal strength due to the color AGC.
	//
	// Eyeballed scope traces show the chroma amplitude to be about 30% of full luma
	// amplitude.
	//
	//
	float chromaSignalAmplitude = 0.5f / std::max<float>(0.10f, params.mArtifactSat);
	float chromaSignalInvAmplitude = params.mArtifactSat * 2.0f;

	float phadjust = -params.mArtifactHue * (nsVDMath::kfTwoPi / 360.0f) + nsVDMath::kfPi * 1.25f;

	float cp = cosf(phadjust);
	float sp = sinf(phadjust);

	float co_ir = 0.956f;
	float co_qr = 0.620f;
	float co_ig = -0.272f;
	float co_qg = -0.647f;
	float co_ib = -1.108f;
	float co_qb = 1.705f;
	rotate(co_ir, co_qr, -mColorParams.mRedShift * (nsVDMath::kfPi / 180.0f));
	rotate(co_ig, co_qg, -mColorParams.mGrnShift * (nsVDMath::kfPi / 180.0f));
	rotate(co_ib, co_qb, -mColorParams.mBluShift * (nsVDMath::kfPi / 180.0f));
	co_ir *= mColorParams.mRedScale;
	co_qr *= mColorParams.mRedScale;
	co_ig *= mColorParams.mGrnScale;
	co_qg *= mColorParams.mGrnScale;
	co_ib *= mColorParams.mBluScale;
	co_qb *= mColorParams.mBluScale;

	rotate(co_ir, co_qr, cp, -sp);
	rotate(co_ig, co_qg, cp, -sp);
	rotate(co_ib, co_qb, cp, -sp);

	const float saturationScale = params.mSaturation * 2;

	const vdfloat3 co_i = vdfloat3 { co_ir, co_ig, co_ib };
	const vdfloat3 co_q = vdfloat3 { co_qr, co_qg, co_qb };

	auto decodeChromaRGB = [=](float i, float q) {
		return i*co_i + q*co_q;
	};

	float lumaRamp[16];
	ATComputeLumaRamp(params.mLumaRampMode, lumaRamp);

	// chroma processing
	for(int i=0; i<15; ++i) {
		float chromatab[4];
		float phase = phadjust + nsVDMath::kfTwoPi * ((params.mHueStart / 360.0f) + (float)i / 15.0f * (params.mHueRange / 360.0f));

		// create chroma signal
		for(int j=0; j<4; ++j) {
			float v = sinf((0.25f * nsVDMath::kfTwoPi * j) - phase);

			chromatab[j] = v;
		}

		float c0 = chromatab[0];
		float c1 = chromatab[1];
		float c2 = chromatab[2];
		float c3 = chromatab[3];

		// multiply chroma signal by pixel pulse
		const float chromaSharp = 0.50f;
		float t[28] = {0};
		t[11-5] = c3 * ((1.0f - chromaSharp) / 3.0f);
		t[12-5] = c0 * ((2.0f + chromaSharp) / 3.0f);
		t[13-5] = c1;
		t[14-5] = c2;
		t[15-5] = c3 * ((2.0f + chromaSharp) / 3.0f);
		t[16-5] = c0 * ((1.0f - chromaSharp) / 3.0f);

		vdfloat3 rgbtab[2][22];

		if (mbTintColorEnabled) {
			for(auto& phaseArray : rgbtab)
				std::fill(std::begin(phaseArray), std::end(phaseArray), vdfloat3{});

			const float sensitivityFactor = 0.50f;
			for(int j=0; j<6; ++j) {
				vdfloat3 c = (t[6+j] * (chromaSignalAmplitude * sensitivityFactor)) * mTintColor;

				rgbtab[0][6+j] = -c;
				rgbtab[1][6+j] = c;
			}
		} else {
			float ytab[22] = {0};
			float itab[22] = {0};
			float qtab[22] = {0};

			ytab[ 7-1] = 0;
			ytab[ 8-1] = (              1*c2 + 2*c3) * (1.0f / 16.0f);
			ytab[ 9-1] = (1*c0 + 2*c1 + 1*c2 + 0*c3) * (1.0f / 16.0f);
			ytab[10-1] = (1*c0 + 0*c1 + 2*c2 + 4*c3) * (1.0f / 16.0f);
			ytab[11-1] = (2*c0 + 4*c1 + 2*c2 + 0*c3) * (1.0f / 16.0f);
			ytab[12-1] = (2*c0 + 0*c1 + 1*c2 + 2*c3) * (1.0f / 16.0f);
			ytab[13-1] = (1*c0 + 2*c1 + 1*c2       ) * (1.0f / 16.0f);
			ytab[14-1] = (1*c0                     ) * (1.0f / 16.0f);

			// demodulate chroma axes by multiplying by sin/cos
			//	t[0] = +I
			//	t[1] = +Q
			//	t[2] = -I
			//	t[3] = -Q
			//
			for(int j=0; j<26; ++j) {
				if (j & 2)
					t[j] = -t[j];
			}

			// apply low-pass filter to chroma
			float u[28] = {0};

			for(int j=6; j<28; ++j) {
				u[j] = (  1 * t[j- 6])
					 + (  0.9732320952f * t[j- 4])
					 + (  0.9732320952f * t[j- 2])
					 + (  1 * t[j])
					 + (  0.1278410428f * u[j- 2]);
			}

			// compensate for gain from pixel shape filter (4x) and low-pass filter (~4.5x)
			for(float& y : u)
				y = y / 4 / ((2+0.9732320952f*2) / (1 - 0.1278410428f));

			// interpolate chroma
			for(int j=0; j<22; ++j) {
				if (!(j & 1)) {
					itab[j] = (u[j+2] + u[j+4])*0.625f - (u[j] + u[j+6])*0.125f;
					qtab[j] = u[j+3];
				} else {
					itab[j] = u[j+3];
					qtab[j] = (u[j+2] + u[j+4])*0.625f - (u[j] + u[j+6])*0.125f;
				}
			}

			for(int j=0; j<22; ++j) {
				// This is currently disabled because for a while we had a bug where it was
				// getting cancelled out, and frankly it looks better without it.
				float fy = 0; //ytab[j] * chromaSignalAmplitude;

				float fi = itab[j];
				float fq = qtab[j];

				vdfloat3 fc = (fi*co_i + fq*co_q) * saturationScale;
				vdfloat3 f0 = fc - fy;
				vdfloat3 f1 = fc + fy;

				rgbtab[0][j] = f0 * params.mIntensityScale;
				rgbtab[1][j] = f1 * params.mIntensityScale;
			}
		}

		for(int k=0; k<2; ++k) {
			for(int j=0; j<2; ++j) {
				rgbtab[k][j+14] += rgbtab[k][j+18];
				rgbtab[k][j+18] = { 0, 0, 0 };
			}

			for(int j=0; j<22; ++j)
				chroma_to_rgb[i+1][k][j] = rgbtab[k][j];
		}
	}

	if (debugOut) {
		using namespace nsVDMath;
		VDStringA s;

		for(int channel = 0; channel < 3; ++channel) {
			vdfloat3 channelSelect(channel == 0, channel == 1, channel == 2);

			for(int phase = 0; phase < 2; ++phase) {
				for(int hue = 0; hue < 16; ++hue) {
					s.sprintf("chroma_%d_%c%X", phase, "rgb"[channel], hue);
					for(int offset = 0; offset < 24; ++offset)
						s.append_sprintf(", %6.3f", dot(chroma_to_rgb[hue][phase][offset], channelSelect));

					*debugOut <<= s.c_str();
				}
			}

			for(int hue = 0; hue < 16; ++hue) {
				s.sprintf("chroma_%c%X  ", "rgb"[channel], hue);
				for(int offset = 0; offset < 24; ++offset) {
					const auto c0 = chroma_to_rgb[hue][0][offset];
					const auto c1 = offset < 22 ? chroma_to_rgb[hue][1][offset + 2] : vdfloat3{};

					s.append_sprintf(", %6.3f", dot(c0 + c1, channelSelect));
				}

				*debugOut <<= s.c_str();
			}
		}
	}

	////////////////////////// 28MHz SECTION //////////////////////////////

	const float lumaSharpness = params.mArtifactSharpness;
	float lumapulse[16] = {
		(1.0f - lumaSharpness) / 3.0f,
		(2.0f + lumaSharpness) / 3.0f,
		(2.0f + lumaSharpness) / 3.0f,
		(1.0f - lumaSharpness) / 3.0f,
	};

	for(int i=0; i<16; ++i) {
		float y = lumaRamp[i] * params.mContrast + params.mBrightness;

		float t[30] = {0};
		t[11] = y*((1.0f - 1.0f)/3.0f);
		t[12] = y*((2.0f + 1.0f)/3.0f);
		t[13] = y*((2.0f + 1.0f)/3.0f);
		t[14] = y*((1.0f - 1.0f)/3.0f);

		for(int j=0; j<30; ++j) {
			if (!(j & 2))
				t[j] = -t[j];
		}

		float u[28] = {0};

		for(int j=4; j<20; ++j) {
			u[j] = (t[j-4] * 0.25f + t[j-2]*0.625f + t[j]*0.75f + t[j+2]*0.625f + t[j+4]*0.25f) / 10.0f;
		}

		// Form luma pulse (14MHz)
		float ytab[22] = {0};
		for(int j=0; j<11; ++j)
			ytab[7+j] = y * lumapulse[j];

		vdfloat3 rgbtab[2][22];

		if (mbTintColorEnabled) {
			for(int j=0; j<22; ++j) {
				rgbtab[0][j] = rgbtab[1][j] = (ytab[j] * params.mIntensityScale) * mTintColor;
			}
		} else {
			// demodulate to i/q
			float itab[22] = {0};
			float qtab[22] = {0};

			for(int j=0; j<22; ++j) {
				if ((j & 1)) {
					itab[j] = (u[j+2] + u[j+4]) * 0.575f - (u[j] + u[j+6]) * 0.065f;
					qtab[j] = u[j+3];
				} else {
					itab[j] = u[j+3];
					qtab[j] = (u[j+2] + u[j+4]) * 0.575f - (u[j] + u[j+6]) * 0.065f;
				}
			}

			// subtract chroma signal from luma
			const float antiChromaScale = 1.3333333f + lumaSharpness * 2.666666f;
			for(int j=0; j<22; ++j) {
				float cs = cosf((0.25f * nsVDMath::kfTwoPi) * (j+2));
				float sn = sinf((0.25f * nsVDMath::kfTwoPi) * (j+2));

				ytab[j] -= (cs*itab[j] + sn*qtab[j]) * antiChromaScale;
			}

			for(int j=0; j<22; ++j) {
				float fy = ytab[j];
				float fi = itab[j];
				float fq = qtab[j];

				vdfloat3 fc = (fi*co_i + fq*co_q) * chromaSignalInvAmplitude;
				vdfloat3 f0 = fy + fc;
				vdfloat3 f1 = fy - fc;

				rgbtab[0][j] = f0 * params.mIntensityScale;
				rgbtab[1][j] = f1 * params.mIntensityScale;
			}
		}

		for(int k=0; k<2; ++k) {
			for(int j=0; j<4; ++j) {
				rgbtab[k][j+14] += rgbtab[k][j+18];
				rgbtab[k][j+18] = { 0, 0, 0 };
			}

			for(int j=0; j<22; ++j)
				y_to_rgb[i][k][j] = rgbtab[k][j];
		}
	}

	// At this point we have all possible luma and chroma kernels computed. Add the luma and chroma
	// kernels together to produce all 256 color kernels in both phases. We then need to produce a
	// few variants:
	//
	//	- For SSE2, double the phases to 4 and add 2/4/6 output pixel delays.
	//	- Create a set of 'twin' kernels that correspond to paired pixels with the same color.
	//	  This is used to accelerate 160 resolution graphics.
	//	- For SSE2, create another set of 'quad' kernels that correspond to paired matching color
	//	  clocks. This is used to accelerate solid bands of color.

	const float encodingScale = 16.0f * 255.0f * (signedOutput ? 0.5f : 1.0f);

#if defined(VD_CPU_AMD64) || defined(VD_CPU_ARM64)
	if (true) {
#else
	if (SSE2_enabled) {
#endif
		memset(&m4x, 0, sizeof m4x);

		for(int idx=0; idx<256; ++idx) {
			int cidx = idx >> 4;
			int lidx = idx & 15;

			const auto& c_waves = chroma_to_rgb[cidx];
			const auto& y_waves = y_to_rgb[lidx];

			auto& VDRESTRICT impulseR = m4x.mPalToR[idx];
			auto& VDRESTRICT impulseG = m4x.mPalToG[idx];
			auto& VDRESTRICT impulseB = m4x.mPalToB[idx];
			auto& VDRESTRICT impulseTwinR = m4x.mPalToRTwin[idx];
			auto& VDRESTRICT impulseTwinG = m4x.mPalToGTwin[idx];
			auto& VDRESTRICT impulseTwinB = m4x.mPalToBTwin[idx];
			auto& VDRESTRICT impulseQuadR = m4x.mPalToRQuad[idx];
			auto& VDRESTRICT impulseQuadG = m4x.mPalToGQuad[idx];
			auto& VDRESTRICT impulseQuadB = m4x.mPalToBQuad[idx];

			vdfloat3 e0[16] {}, e1[16] {};

			for(int k=0; k<4; ++k) {
				// Try to optimize color quality a bit. The hue can only change at pairs of pixels due to GTIA's design,
				// so include the chroma signal for both pixels in the first pixel's impulse.

				if (k & 1) {
					for(int i=0; i<16; ++i) {
						const int j0 = (i-k)*2+0;
						const int j1 = (i-k)*2+1;

						vdfloat3 pal_to_rgb0 = (j0 >= 0 && j0 < 24 ? y_waves[1][j0] : vdfloat3{}) * encodingScale + e0[i];
						vdfloat3 pal_to_rgb1 = (j1 >= 0 && j1 < 24 ? y_waves[1][j1] : vdfloat3{}) * encodingScale + e1[i];

						if (k == 3 && i >= 4) {
							pal_to_rgb0 += e0[i - 4];
							pal_to_rgb1 += e1[i - 4];
						}

						const int r0 = VDRoundToInt32(pal_to_rgb0.x);
						const int g0 = VDRoundToInt32(pal_to_rgb0.y);
						const int b0 = VDRoundToInt32(pal_to_rgb0.z);
						const int r1 = VDRoundToInt32(pal_to_rgb1.x);
						const int g1 = VDRoundToInt32(pal_to_rgb1.y);
						const int b1 = VDRoundToInt32(pal_to_rgb1.z);

						e0[i] = pal_to_rgb0 - vdfloat3{(float)r0, (float)g0, (float)b0};
						e1[i] = pal_to_rgb1 - vdfloat3{(float)r1, (float)g1, (float)b1};

						impulseR[k][i] = (r0 & 0xffff) + ((uint32)r1 << 16);
						impulseG[k][i] = (g0 & 0xffff) + ((uint32)g1 << 16);
						impulseB[k][i] = (b0 & 0xffff) + ((uint32)b1 << 16);
					}
				} else {
					for(int i=0; i<16; ++i) {
						const int j0 = (i-k)*2+0;
						const int j1 = (i-k)*2+1;
						vdfloat3 pal_to_rgb0 = (j0 >= 0 && j0 < 24 ? c_waves[0][j0] + y_waves[0][j0] : vdfloat3{});
						vdfloat3 pal_to_rgb1 = (j1 >= 0 && j1 < 24 ? c_waves[0][j1] + y_waves[0][j1] : vdfloat3{});

						if (j0 >= 2 && j0 < 26) pal_to_rgb0 += c_waves[1][j0-2];
						if (j1 >= 2 && j1 < 26) pal_to_rgb1 += c_waves[1][j1-2];

						pal_to_rgb0 = pal_to_rgb0 * encodingScale + e0[i];
						pal_to_rgb1 = pal_to_rgb1 * encodingScale + e1[i];

						const int r0 = VDRoundToInt32(pal_to_rgb0.x);
						const int g0 = VDRoundToInt32(pal_to_rgb0.y);
						const int b0 = VDRoundToInt32(pal_to_rgb0.z);
						const int r1 = VDRoundToInt32(pal_to_rgb1.x);
						const int g1 = VDRoundToInt32(pal_to_rgb1.y);
						const int b1 = VDRoundToInt32(pal_to_rgb1.z);

						e0[i] = pal_to_rgb0 - vdfloat3{(float)r0, (float)g0, (float)b0};
						e1[i] = pal_to_rgb1 - vdfloat3{(float)r1, (float)g1, (float)b1};

						impulseR[k][i] = (r0 & 0xffff) + ((uint32)r1 << 16);
						impulseG[k][i] = (g0 & 0xffff) + ((uint32)g1 << 16);
						impulseB[k][i] = (b0 & 0xffff) + ((uint32)b1 << 16);
					}
				}
			}

			const auto twinAdd = [](uint32 x, uint32 y) -> uint32 {
				return (x & 0x7FFF7FFF) + (y & 0x7FFF7FFF) ^ ((x ^ y) & 0x80008000);
			};

			const uint32 bias = signedOutput ? 0x0408'0408 : 0x0008'0008;
			impulseR[0][0] = twinAdd(impulseR[0][0], bias);
			impulseR[0][1] = twinAdd(impulseR[0][1], bias);
			impulseR[0][2] = twinAdd(impulseR[0][2], bias);
			impulseR[0][3] = twinAdd(impulseR[0][3], bias);
			impulseG[0][0] = twinAdd(impulseG[0][0], bias);
			impulseG[0][1] = twinAdd(impulseG[0][1], bias);
			impulseG[0][2] = twinAdd(impulseG[0][2], bias);
			impulseG[0][3] = twinAdd(impulseG[0][3], bias);
			impulseB[0][0] = twinAdd(impulseB[0][0], bias);
			impulseB[0][1] = twinAdd(impulseB[0][1], bias);
			impulseB[0][2] = twinAdd(impulseB[0][2], bias);
			impulseB[0][3] = twinAdd(impulseB[0][3], bias);

			for(int i=0; i<16; ++i) {
				impulseTwinR[0][i] = twinAdd(impulseR[0][i], impulseR[1][i]);
				impulseTwinG[0][i] = twinAdd(impulseG[0][i], impulseG[1][i]);
				impulseTwinB[0][i] = twinAdd(impulseB[0][i], impulseB[1][i]);
			}

			for(int i=0; i<16; ++i) {
				impulseTwinR[1][i] = twinAdd(impulseR[2][i], impulseR[3][i]);
				impulseTwinG[1][i] = twinAdd(impulseG[2][i], impulseG[3][i]);
				impulseTwinB[1][i] = twinAdd(impulseB[2][i], impulseB[3][i]);
			}

			for(int i=0; i<16; ++i) {
				impulseQuadR[i] = twinAdd(impulseTwinR[0][i], impulseTwinR[1][i]);
				impulseQuadG[i] = twinAdd(impulseTwinG[0][i], impulseTwinG[1][i]);
				impulseQuadB[i] = twinAdd(impulseTwinB[0][i], impulseTwinB[1][i]);
			}
		}
	} else {
		memset(&m2x, 0, sizeof m2x);

		for(int idx=0; idx<256; ++idx) {
			int cidx = idx >> 4;
			int lidx = idx & 15;

			for(int k=0; k<2; ++k) {
				for(int i=0; i<10; ++i) {
					vdfloat3 pal_to_rgb0 = (chroma_to_rgb[cidx][k][i*2+0] + y_to_rgb[lidx][k][i*2+0]) * encodingScale;
					vdfloat3 pal_to_rgb1 = (chroma_to_rgb[cidx][k][i*2+1] + y_to_rgb[lidx][k][i*2+1]) * encodingScale;

					m2x.mPalToR[idx][k][i+k] = VDRoundToInt32(pal_to_rgb0.x) + (VDRoundToInt32(pal_to_rgb1.x) << 16);
					m2x.mPalToG[idx][k][i+k] = VDRoundToInt32(pal_to_rgb0.y) + (VDRoundToInt32(pal_to_rgb1.y) << 16);
					m2x.mPalToB[idx][k][i+k] = VDRoundToInt32(pal_to_rgb0.z) + (VDRoundToInt32(pal_to_rgb1.z) << 16);
				}
			}

			if (signedOutput) {
				m2x.mPalToR[idx][0][0] += 0x0400'0400;
				m2x.mPalToR[idx][0][1] += 0x0400'0400;
				m2x.mPalToG[idx][0][0] += 0x0400'0400;
				m2x.mPalToG[idx][0][1] += 0x0400'0400;
				m2x.mPalToB[idx][0][0] += 0x0400'0400;
				m2x.mPalToB[idx][0][1] += 0x0400'0400;
			}

			for(int i=0; i<12; ++i) {
				m2x.mPalToRTwin[idx][i] = m2x.mPalToR[idx][0][i] + m2x.mPalToR[idx][1][i];
				m2x.mPalToGTwin[idx][i] = m2x.mPalToG[idx][0][i] + m2x.mPalToG[idx][1][i];
				m2x.mPalToBTwin[idx][i] = m2x.mPalToB[idx][0][i] + m2x.mPalToB[idx][1][i];
			}
		}
	}
}

void ATArtifactingEngine::RecomputePALTables(ATConsoleOutput *debugOut) {
	const ATColorParams& params = mColorParams;
	const bool signedOutput = mbExpandedRangeOutput;

	mbHighNTSCTablesInited = false;
	mbHighPALTablesInited = true;
	mbHighTablesSigned = signedOutput;

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

	const float sat2 = mColorParams.mArtifactSat;
	const float sat1 = mColorParams.mSaturation / std::max<float>(0.001f, sat2);

	const float chromaPhaseStep = -mColorParams.mHueRange * (nsVDMath::kfTwoPi / (360.0f * 15.0f));

	// UV<->RGB chroma coefficients.
	const float co_vr = 1.1402509f;
	const float co_ub = 2.0325203f;

	float utab[2][16];
	float vtab[2][16];
	float ytab[16];

	utab[0][0] = 0;
	utab[1][0] = 0;
	vtab[0][0] = 0;
	vtab[1][0] = 0;

	// The hue start is in I-Q, which we must convert to the U-V plane.
	const float chromaPhaseOffset = (123.0f - mColorParams.mHueStart) * (nsVDMath::kfTwoPi / 360.0f);

	for(int i=0; i<15; ++i) {
		const ATPALPhaseInfo& palPhaseInfo = kATPALPhaseLookup[i];
		float t1 = chromaPhaseOffset + palPhaseInfo.mEvenPhase * chromaPhaseStep;
		float t2 = chromaPhaseOffset + palPhaseInfo.mOddPhase * chromaPhaseStep;

		utab[0][i+1] = -cosf(t1)*palPhaseInfo.mEvenInvert;
		vtab[0][i+1] =  sinf(t1)*palPhaseInfo.mEvenInvert;
		utab[1][i+1] = -cosf(t2)*palPhaseInfo.mOddInvert;
		vtab[1][i+1] = -sinf(t2)*palPhaseInfo.mOddInvert;		// V inversion
	}

	for(int i=0; i<16; ++i) {
		ytab[i] = mColorParams.mBrightness + mColorParams.mContrast * lumaRamp[i];
	}

	ATFilterKernel kernbase;
	ATFilterKernel kerncfilt;
	ATFilterKernel kernumod;
	ATFilterKernel kernvmod;

	// Box filter representing pixel time.
	kernbase.Init(0, { 1, 1, 1, 1, 1 });

	kernbase *= params.mIntensityScale;

	// Chroma low-pass filter. We apply this before encoding to avoid excessive bleeding
	// into luma.
	kerncfilt.Init(-5, {
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
		  1.0f / 1024.0f
	});

	// Modulation filters -- sine and cosine of color subcarrier. We also apply chroma
	// amplitude adjustment here.
	constexpr float ivrt2 = 0.70710678118654752440084436210485f;
	kernumod.Init(0, { 1, ivrt2, 0, -ivrt2, -1, -ivrt2, 0, ivrt2 });
	kernumod *= sat1;
	kernvmod.Init(0, { 0, ivrt2, 1, ivrt2, 0, -ivrt2, -1, -ivrt2 });
	kernvmod *= sat1;

	ATFilterKernel kernysep;
	ATFilterKernel kerncsep;
	ATFilterKernel kerncdemod;

	// Luma separation filter -- just a box filter.
	kernysep.Init(-4, {
		0.5f / 8.0f,
		1.0f / 8.0f,
		1.0f / 8.0f,
		1.0f / 8.0f,
		1.0f / 8.0f,
		1.0f / 8.0f,
		1.0f / 8.0f,
		1.0f / 8.0f,
		0.5f / 8.0f
	});

	// Chroma separation filter -- dot with peaks of sine/cosine waves and apply box filter.
	kerncsep.Init(-16, { 1,0,0,0,-2,0,0,0,2,0,0,0,-2,0,0,0,2,0,0,0,-2,0,0,0,2,0,0,0,-2,0,0,0,1 });
	kerncsep *= 1.0f / 16.0f;

	// Demodulation filter. Here we invert every other sample of extracted U and V.
	kerncdemod.Init(0, {
		 -1,
		 -1,
		 1,
		 1,
		 1,
		 1,
		 -1,
		 -1
	});

	kerncdemod *= sat2 * 0.5f;	// 0.5 is for chroma line averaging

	memset(&mPal8x, 0, sizeof mPal8x);

	// The 90d phase shift is here to compensate for a +2 hires pixel shift we
	// apply on the source 7MHz data at the start of processing to deal with
	// alignment issues.
	const float ycphase = (mColorParams.mArtifactHue + 90.0f) * nsVDMath::kfPi / 180.0f;
	const float ycphasec = cosf(ycphase);
	const float ycphases = sinf(ycphase);

#if VD_CPU_X86 || VD_CPU_X64
	const auto twinAdd = [](uint32 x, uint32 y) -> uint32 {
		return (x & 0x7FFF7FFF) + (y & 0x7FFF7FFF) ^ ((x ^ y) & 0x80008000);
	};
#endif

	// compute all fused direct and crosstalk encode+decode kernels
	ATFilterKernel kerny2y[8];
	ATFilterKernel kernu2y[8];
	ATFilterKernel kernv2y[8];
	ATFilterKernel kerny2u[8];
	ATFilterKernel kernu2u[8];
	ATFilterKernel kernv2u[8];
	ATFilterKernel kerny2v[8];
	ATFilterKernel kernu2v[8];
	ATFilterKernel kernv2v[8];

	for(int phase = 0; phase < 8; ++phase) {
		const float yphase = 0;
		const float cphase = 0;

		ATFilterKernel kernbase2 = kernbase >> (5*phase);
		ATFilterKernel kernsignaly = kernbase2;

		// downsample chroma and modulate
		ATFilterKernel kernsignalu = (kernbase2 * kerncfilt) ^ kernumod;
		ATFilterKernel kernsignalv = (kernbase2 * kerncfilt) ^ kernvmod;

		// extract Y via low pass filter
		kerny2y[phase] = ATFilterKernelSampleBicubic(kernsignaly * kernysep, yphase, 2.5f, -0.75f);
		kernu2y[phase] = ATFilterKernelSampleBicubic(kernsignalu * kernysep, yphase, 2.5f, -0.75f);
		kernv2y[phase] = ATFilterKernelSampleBicubic(kernsignalv * kernysep, yphase, 2.5f, -0.75f);

		// separate, low pass filter and demodulate chroma
		ATFilterKernel u = ATFilterKernelSampleBicubic(ATFilterKernelSamplePoint((kernsignaly * kerncsep) ^ kerncdemod, 0, 4), cphase     , 0.625f, -0.75f);
		kernu2u[phase] = ATFilterKernelSampleBicubic(ATFilterKernelSamplePoint((kernsignalu * kerncsep) ^ kerncdemod, 0, 4), cphase     , 0.625f, -0.75f);
		kernv2u[phase] = ATFilterKernelSampleBicubic(ATFilterKernelSamplePoint((kernsignalv * kerncsep) ^ kerncdemod, 0, 4), cphase     , 0.625f, -0.75f);

		ATFilterKernel v = ATFilterKernelSampleBicubic(ATFilterKernelSamplePoint((kernsignaly * kerncsep) ^ kerncdemod, 2, 4), cphase-0.5f, 0.625f, -0.75f);
		kernu2v[phase] = ATFilterKernelSampleBicubic(ATFilterKernelSamplePoint((kernsignalu * kerncsep) ^ kerncdemod, 2, 4), cphase-0.5f, 0.625f, -0.75f);
		kernv2v[phase] = ATFilterKernelSampleBicubic(ATFilterKernelSamplePoint((kernsignalv * kerncsep) ^ kerncdemod, 2, 4), cphase-0.5f, 0.625f, -0.75f);

		kerny2u[phase] = u * ycphasec - v * ycphases;
		kerny2v[phase] = u * ycphases + v * ycphasec;

		if (mbTintColorEnabled) {
			const float sensitivityFactor = 0.50f;

			kernu2y[phase] = ATFilterKernelSampleBicubic(kernsignalu * ycphasec - kernsignalv * ycphases, yphase, 2.5f, -0.75f) * sensitivityFactor;
			kernv2y[phase] = ATFilterKernelSampleBicubic(kernsignalu * ycphases + kernsignalv * ycphasec, yphase, 2.5f, -0.75f) * sensitivityFactor;
			kerny2u[phase] *= 0;
			kerny2v[phase] *= 0;
			kernu2u[phase] *= 0;
			kernu2v[phase] *= 0;
			kernv2u[phase] *= 0;
			kernv2v[phase] *= 0;
		}
	}

	for(int k=0; k<2; ++k) {
		float v_invert = k ? -1.0f : 1.0f;

		for(int j=0; j<256; ++j) {
			float u = utab[k][j >> 4];
			float v = vtab[k][j >> 4];
			float y = ytab[j & 15];

			float scale = 64.0f * 255.0f;

			if (signedOutput)
				scale *= 0.5f;

			// Error control
			//
			// Since we operate with relatively low 10.6 precision and have as many as 12 overlapping
			// kernels contributing to each output pixel, it is critical to keep accumulated error as
			// low as possible. Getting as much as +/-6 ulps of error in each of Y, U, and V is enough
			// to proc a bunch of +/-1 errors in the final output for solid colors, which is ugly. To
			// improve this, we track and accumulate errors across phases to reduce the cumulative
			// error when the same pixel color is applied across multiple phases, with the idea that
			// this case is much more important and noticeable than with different pixels.
			//
			// In scalar mode, we accumulate luma kernels like this (each row is a source hires pixel,
			// each column is a pair of half-hires output pixels):
			//
			//		Luma                Chroma
			//		0123456789ABCDEF    0123456789ABCDEF
			//		< 0>                <---  0 --->
			//		 < 1>                <---  1 --->
			//		  < 2>                <---  2 --->
			//		   < 3>                <---  3 --->
			//		    < 4>                <---  4 --->
			//		     < 5>                <---  5 --->
			//		      < 6>                <---  6 --->
			//		       < 7>                <---  7 --->
			//		        < 0>                <---  0 --->
			//
			// The kernels advance a pair of output pixels each time and repeat every 16 output pixels, so 
			// we accumulate errors in a rotating ring of 16 locations, advancing by 2 each time. As each
			// kernel value is rounded, no more than +/-0.5ulp propagates in each location.
			//
			// In SSE2, the situation is a bit more complicated because of preshifted kernels:
			//		Luma                Chroma
			//		0123456789ABCDEF    0123456789ABCDEF
			//		< 0>----            <---  0 --->----
			//		-< 1>---            -<---  1 --->---
			//		--< 2>--            --<---  2 --->--
			//		---< 3>-            ---<---  3 --->-
			//		    < 4>----            <---  4 --->----
			//		    -< 5>---            -<---  5 --->---
			//		    --< 6>--            --<---  6 --->--
			//		    ---< 7>-            ---<---  7 --->-
			//		        < 0>----        ----<---  0 --->----
			//
			// However, this is just zero padding on the ends and error correction proceeds the same way.
			//
			float yerror[8][2] {};
			float uerror[8][2] {};
			float verror[8][2] {};

			for(int phase = 0; phase < 8; ++phase) {
				float p2yw[8 + 8] = {0};
				float p2uw[24 + 8] = {0};
				float p2vw[24 + 8] = {0};

#if VD_CPU_X86 || VD_CPU_X64
				if (SSE2_enabled) {
					const int phase4 = phase & 4;
					int ypos = 4 - phase4*2;
					int cpos = 13 - phase4*2;

					ATFilterKernelAccumulateWindow(kerny2y[phase], p2yw, ypos, 16, y);
					ATFilterKernelAccumulateWindow(kernu2y[phase], p2yw, ypos, 16, u);
					ATFilterKernelAccumulateWindow(kernv2y[phase], p2yw, ypos, 16, v);

					ATFilterKernelAccumulateWindow(kerny2u[phase], p2uw, cpos, 32, y * co_ub);
					ATFilterKernelAccumulateWindow(kernu2u[phase], p2uw, cpos, 32, u * co_ub);
					ATFilterKernelAccumulateWindow(kernv2u[phase], p2uw, cpos, 32, v * co_ub);

					ATFilterKernelAccumulateWindow(kerny2v[phase], p2vw, cpos, 32, y * co_vr * v_invert);
					ATFilterKernelAccumulateWindow(kernu2v[phase], p2vw, cpos, 32, u * co_vr * v_invert);
					ATFilterKernelAccumulateWindow(kernv2v[phase], p2vw, cpos, 32, v * co_vr * v_invert);

					if (debugOut) {
						using namespace nsVDMath;
						VDStringA s;

						if (j < 16) {
							s.sprintf("y_%d_%d_%X  ", k, phase, j);

							for(int offset = 0; offset < 16; ++offset)
								s.append_sprintf(", %6.3f", p2yw[offset]);

							*debugOut <<= s.c_str();
						}

						if ((j & 15) == 15) {
							for(int channel = 0; channel < 2; ++channel) {
								s.sprintf("%c_%d_%d_%X  ", "uv"[channel], k, phase, j >> 4);

								const float *filt = channel ? p2vw : p2uw;
								for(int offset = 0; offset < 32; ++offset)
									s.append_sprintf(", %6.3f", filt[offset]);

								*debugOut <<= s.c_str();
							}
						}
					}

					uint32 *kerny16 = mPal8x.mPalToY[k][j][phase];
					uint32 *kernu16 = mPal8x.mPalToU[k][j][phase];
					uint32 *kernv16 = mPal8x.mPalToV[k][j][phase];

					for(int offset=0; offset<8; ++offset) {
						float fw0 = p2yw[offset*2+0] * scale + yerror[offset][0];
						float fw1 = p2yw[offset*2+1] * scale + yerror[offset][1];
						
						sint32 w0 = VDRoundToInt32(fw0);
						sint32 w1 = VDRoundToInt32(fw1);
						
						yerror[offset][0] = fw0 - (float)w0;
						yerror[offset][1] = fw1 - (float)w1;

						kerny16[offset] = ((uint32)w1 << 16) + (w0 & 0xffff);
					}

					kerny16[phase & 3] = twinAdd(kerny16[phase & 3], signedOutput ? 0x1020'1020 : 0x0020'0020);

					for(int offset=0; offset<16; ++offset) {
						float fw0 = p2uw[offset*2+0] * scale + uerror[offset & 7][0];
						float fw1 = p2uw[offset*2+1] * scale + uerror[offset & 7][1];

						sint32 w0 = VDRoundToInt32(fw0);
						sint32 w1 = VDRoundToInt32(fw1);

						uerror[offset & 7][0] = fw0 - (float)w0;
						uerror[offset & 7][1] = fw1 - (float)w1;

						kernu16[offset] = ((uint32)w1 << 16) + (w0 & 0xffff);
					}

					for(int offset=0; offset<16; ++offset) {
						float fw0 = p2vw[offset*2+0] * scale + verror[offset & 7][0];
						float fw1 = p2vw[offset*2+1] * scale + verror[offset & 7][1];

						sint32 w0 = VDRoundToInt32(fw0);
						sint32 w1 = VDRoundToInt32(fw1);

						verror[offset & 7][0] = fw0 - (float)w0;
						verror[offset & 7][1] = fw1 - (float)w1;

						kernv16[offset] = ((uint32)w1 << 16) + (w0 & 0xffff);
					}

					// Phases 4-7 are written 4 pixel pairs (128 bits) ahead, so rotate the errors
					// around at the halfway point.
					if (phase == 3) {
						std::swap_ranges(yerror, yerror + 4, yerror + 4);
						std::swap_ranges(uerror, uerror + 4, uerror + 4);
						std::swap_ranges(verror, verror + 4, verror + 4);
					}
				} else
#endif
				{
					int ypos = 4 - phase*2;
					int cpos = 13 - phase*2;

					ATFilterKernelAccumulateWindow(kerny2y[phase], p2yw, ypos, 8, y);
					ATFilterKernelAccumulateWindow(kernu2y[phase], p2yw, ypos, 8, u);
					ATFilterKernelAccumulateWindow(kernv2y[phase], p2yw, ypos, 8, v);

					ATFilterKernelAccumulateWindow(kerny2u[phase], p2uw, cpos, 24, y * co_ub);
					ATFilterKernelAccumulateWindow(kernu2u[phase], p2uw, cpos, 24, u * co_ub);
					ATFilterKernelAccumulateWindow(kernv2u[phase], p2uw, cpos, 24, v * co_ub);

					ATFilterKernelAccumulateWindow(kerny2v[phase], p2vw, cpos, 24, y * co_vr * v_invert);
					ATFilterKernelAccumulateWindow(kernu2v[phase], p2vw, cpos, 24, u * co_vr * v_invert);
					ATFilterKernelAccumulateWindow(kernv2v[phase], p2vw, cpos, 24, v * co_vr * v_invert);

					uint32 *kerny16 = mPal2x.mPalToY[k][j][phase];
					uint32 *kernu16 = mPal2x.mPalToU[k][j][phase];
					uint32 *kernv16 = mPal2x.mPalToV[k][j][phase];

					for(int offset=0; offset<4; ++offset) {
						float fw0 = p2yw[offset*2+0] * scale + yerror[(offset + phase) & 7][0];
						float fw1 = p2yw[offset*2+1] * scale + yerror[(offset + phase) & 7][1];
						sint32 w0 = VDRoundToInt32(fw0);
						sint32 w1 = VDRoundToInt32(fw1);
						yerror[(offset + phase) & 7][0] = fw0 - (float)w0;
						yerror[(offset + phase) & 7][1] = fw1 - (float)w1;

						kerny16[offset] = (w1 << 16) + w0;
					}

					kerny16[3] += signedOutput ? 0x50005000 : 0x40004000;

					for(int offset=0; offset<12; ++offset) {
						float fw0 = p2uw[offset*2+0] * scale + uerror[(offset + phase) & 7][0];
						float fw1 = p2uw[offset*2+1] * scale + uerror[(offset + phase) & 7][1];
						sint32 w0 = VDRoundToInt32(fw0);
						sint32 w1 = VDRoundToInt32(fw1);
						uerror[(offset + phase) & 7][0] = fw0 - (float)w0;
						uerror[(offset + phase) & 7][1] = fw1 - (float)w1;

						kernu16[offset] = (w1 << 16) + w0;
					}

					for(int offset=0; offset<12; ++offset) {
						float fw0 = p2vw[offset*2+0] * scale + verror[(offset + phase) & 7][0];
						float fw1 = p2vw[offset*2+1] * scale + verror[(offset + phase) & 7][1];
						sint32 w0 = VDRoundToInt32(fw0);
						sint32 w1 = VDRoundToInt32(fw1);
						verror[(offset + phase) & 7][0] = fw0 - (float)w0;
						verror[(offset + phase) & 7][1] = fw1 - (float)w1;

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

#if VD_CPU_X86 || VD_CPU_X64
	if (SSE2_enabled) {
		for(int i=0; i<2; ++i) {
			for(int j=0; j<256; ++j) {
				for(int k=0; k<4; ++k) {
					for(int l=0; l<8; ++l)
						mPal8x.mPalToYTwin[i][j][k][l] = twinAdd(mPal8x.mPalToY[i][j][k*2][l], mPal8x.mPalToY[i][j][k*2+1][l]);

					for(int l=0; l<16; ++l)
						mPal8x.mPalToUTwin[i][j][k][l] = twinAdd(mPal8x.mPalToU[i][j][k*2][l], mPal8x.mPalToU[i][j][k*2+1][l]);

					for(int l=0; l<16; ++l)
						mPal8x.mPalToVTwin[i][j][k][l] = twinAdd(mPal8x.mPalToV[i][j][k*2][l], mPal8x.mPalToV[i][j][k*2+1][l]);
				}
			}
		}
	}
#endif
}

