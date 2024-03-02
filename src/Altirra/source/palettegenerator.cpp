//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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

#include <stdafx.h>
#include "palettegenerator.h"
#include "gtia.h"
#include "gtiatables.h"

void ATColorPaletteGenerator::Generate(const ATColorParams& params, ATMonitorMode monitorMode) {
	using namespace nsVDVecMath;

	const bool palQuirks = params.mbUsePALQuirks;
	float angle = params.mHueStart * (nsVDMath::kfTwoPi / 360.0f);
	float angleStep = params.mHueRange * (nsVDMath::kfTwoPi / (360.0f * 15.0f));
	float gamma = 1.0f / params.mGammaCorrect;

	float lumaRamp[16];

	ATComputeLumaRamp(params.mLumaRampMode, lumaRamp);

	// I/Q -> RGB coefficients
	//
	// There are a ton of matrices posted on the Internet that all vary in
	// small amounts due to roundoff. SMPTE 170M-2004 gives the best full derivation
	// of all the equations. Here's the gist:
	//
	// - Y has two definitions:
	//	 Y = 0.30R + 0.59G + 0.11B (NTSC)
	//   Y = 0.299R + 0.587G + 0.114B (SMPTE 170M-2004)
	//   SMPTE 170M-2004 Annex A indicates that the NTSC specification is
	//   rounded off from the original NTSC derivation.
	// - Also per SMPTE 170M-2004, R-Y and B-Y are scaled by 0.877283... and
	//   0.492111... to place yellow and cyan 75% bars at 100 IRE. This
	//   gives V = 0.877283(R-Y) and U=0.492111(B-Y).
	// - I-Q is rotated 33deg from V-U (*not* U-V).
	//
	// Final Scilab derivation:
	//
	// R=[1 0 0]; G=[0 1 0]; B=[0 0 1];
	// Y=0.299*R+0.587*G+0.114*B;
	// U=0.492111*(B-Y); V=0.877283*(R-Y);
	// cs=cosd(33); sn=sind(33);
	// I=V*cs-U*sn; Q=U*cs+V*sn;
	// inv([Y;I;Q])
	//
	// One benefit of using the precise values is that the angles make sense: B-Y
	// and R-Y are at 123 and 33 degrees in I-Q space, respectively, with exactly
	// 90 degrees between them.
	//
	// The angle and gain of each of these vectors are adjustable. This is equivalent
	// to arbitrarily setting all six elements of the matrix by polar/cartesian
	// equivalence.
	//
	vdfloat2 co_r { 0.956f, 0.621f };
	vdfloat2 co_g { -0.272f, -0.647f };
	vdfloat2 co_b { -1.107f, 1.704f };

	co_r = vdfloat2x2::rotation(params.mRedShift * (nsVDMath::kfPi / 180.0f)) * co_r * params.mRedScale;
	co_g = vdfloat2x2::rotation(params.mGrnShift * (nsVDMath::kfPi / 180.0f)) * co_g * params.mGrnScale;
	co_b = vdfloat2x2::rotation(params.mBluShift * (nsVDMath::kfPi / 180.0f)) * co_b * params.mBluScale;

	static constexpr vdfloat3x3 fromNTSC = vdfloat3x3 {
		{ 0.6068909f, 0.1735011f, 0.2003480f },
		{ 0.2989164f, 0.5865990f, 0.1144845f },
		{ 0.0000000f, 0.0660957f, 1.1162243f },
	}.transpose();

	static constexpr vdfloat3x3 fromPAL = vdfloat3x3 {
		{ 0.4306190f, 0.3415419f, 0.1783091f },
		{ 0.2220379f, 0.7066384f, 0.0713236f },
		{ 0.0201853f, 0.1295504f, 0.9390944f },
	}.transpose();

	static constexpr vdfloat3x3 tosRGB = vdfloat3x3 {
		{  3.2404542f, -1.5371385f, -0.4985314f },
		{ -0.9692660f,  1.8760108f,  0.0415560f },
		{  0.0556434f, -0.2040259f,  1.0572252f },
	}.transpose();

	static constexpr vdfloat3x3 toAdobeRGB = vdfloat3x3 {
		{  2.0413690f, -0.5649464f, -0.3446944f },
		{ -0.9692660f,  1.8760108f,  0.0415560f },
		{  0.0134474f, -0.1183897f,  1.0154096f },
	}.transpose();

	vdfloat3x3 mx;
	bool useMatrix = false;
	const vdfloat3x3 *toMat = nullptr;

	switch(params.mColorMatchingMode) {
		case ATColorMatchingMode::SRGB:
			toMat = &tosRGB;
			break;

		case ATColorMatchingMode::AdobeRGB:
			toMat = &toAdobeRGB;
			break;
	}

	if (toMat) {
		const vdfloat3x3 *fromMat = palQuirks ? &fromPAL : &fromNTSC;

		mx = (*fromMat) * (*toMat);

		useMatrix = true;
	}

	const float nativeGamma = 2.2f;

	uint32 *dst = mPalette;
	uint32 *dst2 = mSignedPalette;

	const bool useColorTint = monitorMode != ATMonitorMode::Color;
	vdfloat32x3 tintColor = vdfloat32x3::zero();

	if (useColorTint)
		useMatrix = false;

	switch(monitorMode) {
		case ATMonitorMode::MonoAmber:
			tintColor = vdfloat32x3{252.0/255.0f, 202/255.0f, 3.0f/255.0f};
			break;

		case ATMonitorMode::MonoGreen:
			tintColor = vdfloat32x3{0.0f/255.0f, 255.0f/255.0f, 32.0f/255.0f};
			break;

		case ATMonitorMode::MonoBluishWhite:
			tintColor = vdfloat32x3{138.0f/255.0f, 194.0f/255.0f, 255.0f/255.0f};
			break;

		case ATMonitorMode::MonoWhite:
			tintColor = vdfloat32x3{255.0f/255.0f, 255.0f/255.0f, 255.0f/255.0f};
			break;

		default:
			break;
	}

	if (monitorMode == ATMonitorMode::Peritel) {
		// The CA061034 PERITEL adapter is a simple translation from GTIA luma lines to RGB output. The
		// adapter is very simple and purely made of passive components; the matrix below is just a guess
		// based on visual screenshots. See: http://www.atari800xl.eu/hardware/computers/peritel-atari-800.html
		static constexpr vdfloat3x3 mx = {
			vdfloat3 { 1.00f, 0.00f, 0.00f },
			vdfloat3 { 0.11f, 0.91f, 0.34f },
			vdfloat3 { 0.11f, 0.47f, 0.99f }
		};

		for(int i=0; i<8; ++i) {
			vdfloat3 c;

			c.x = (i & 1) ? 1.0f : 0.0f;
			c.y = (i & 4) ? 1.0f : 0.0f;
			c.z = (i & 2) ? 1.0f : 0.0f;
			c = c * mx;
			
			c = c * params.mContrast + params.mBrightness;

			if (c.x > 0.0f)
				c.x = powf(c.x, gamma);

			if (c.y > 0.0f)
				c.y = powf(c.y, gamma);

			if (c.z > 0.0f)
				c.z = powf(c.z, gamma);

			c *= params.mIntensityScale;

			dst[0] = dst[1]
				= (VDClampedRoundFixedToUint8Fast((float)c.x) << 16)
				+ (VDClampedRoundFixedToUint8Fast((float)c.y) <<  8)
				+ (VDClampedRoundFixedToUint8Fast((float)c.z)      );

			vdfloat3 c2 = c * 127.0f / 255.0f + 64.0f / 255.0f;

			dst2[0] = dst2[1]
				= (VDClampedRoundFixedToUint8Fast((float)c2.x) << 16)
				+ (VDClampedRoundFixedToUint8Fast((float)c2.y) <<  8)
				+ (VDClampedRoundFixedToUint8Fast((float)c2.z)      );

			dst += 2;
			dst2 += 2;
		}

		for(int i=0; i<15; ++i) {
			memcpy(dst, dst - 16, sizeof(*dst)*16);
			dst += 16;

			memcpy(dst2, dst2 - 16, sizeof(*dst2)*16);
			dst2 += 16;
		}

		memcpy(mUncorrectedPalette, mPalette, sizeof mUncorrectedPalette);
	} else {
		uint32 *dstu = mUncorrectedPalette;

		if (useColorTint)
			tintColor *= 1.0f / dot(tintColor, vdfloat32x3{0.30f, 0.59f, 0.11f});

		for(int hue=0; hue<16; ++hue) {
			float i = 0;
			float q = 0;

			if (hue) {
				if (palQuirks) {
					const ATPALPhaseInfo& palPhaseInfo = kATPALPhaseLookup[hue - 1];

					float angle2 = angle + angleStep * palPhaseInfo.mEvenPhase;
					float angle3 = angle + angleStep * palPhaseInfo.mOddPhase;

					float i2 = cosf(angle2) * palPhaseInfo.mEvenInvert;
					float q2 = sinf(angle2) * palPhaseInfo.mEvenInvert;
					float i3 = cosf(angle3) * palPhaseInfo.mOddInvert;
					float q3 = sinf(angle3) * palPhaseInfo.mOddInvert;

					i = (i2 + i3) * (0.5f * params.mSaturation);
					q = (q2 + q3) * (0.5f * params.mSaturation);
				} else {
					i = params.mSaturation * cos(angle);
					q = params.mSaturation * sin(angle);
					angle += angleStep;
				}
			}

			const vdfloat2 iq { i, q };
			float cr = nsVDMath::dot(iq, co_r);
			float cg = nsVDMath::dot(iq, co_g);
			float cb = nsVDMath::dot(iq, co_b);

			vdfloat32x3 chroma = vdfloat32x3::set(cr, cg, cb);
			vdfloat32x3x3 colorCorrectionMatrix = loadu(mx);

			for(int luma=0; luma<16; ++luma) {
				float y = params.mContrast * lumaRamp[luma] + params.mBrightness;

				vdfloat32x3 rgb0 = y + chroma;
				vdfloat32x3 rgb = rgb0;

				if (useColorTint) {
					// A monochrome monitor does not decode the color signal, but can still display it within the
					// bandwidth limitations of the monitor hardware. Assuming 50% duty cycle on the chroma subcarrier,
					// it will be displayed as a blend in linear color space of (y+c) and (y-c), where c is the luma-relative
					// intensity resulting from the chroma signal. However, we may have to clamp y-c since negative light
					// is impossible. The result:
					//
					//	result = to_gamma((to_linear(max(y-c, 0)) + to_linear(y+c)) * 0.5)
					//	       = (((max(y-c, 0))^2.4 + (y+c)^2.4) * 0.5)^(1/2.4)
					//
					// That leaves the amplitude of the chroma signal, which varies between different computer models.
					// The 800 has the strongest chroma signal, the 800XL is in the middle, the 130XE is the weakest.
					// We use a approximation of 25% of full luma range for simplicity.

					if (hue) {
						const float c = 0.125f;
						rgb = powf((powf(std::max<float>(y - c, 0.0f), 2.4f) + powf(y + c, 2.4f)) * 0.5f, 1.0f / 2.4f) * tintColor;
					} else {
						rgb = y*tintColor;
					}
				} else if (useMatrix) {
					rgb = max(rgb, vdfloat32x3::zero());
					rgb = pow(rgb, nativeGamma);

					rgb = mul(rgb, colorCorrectionMatrix);

					if (params.mColorMatchingMode == ATColorMatchingMode::AdobeRGB) {
						rgb = pow(max0(rgb), 1.0f / 2.2f);
					} else {
						rgb = select(rgb < vdfloat32x3::set1(0.0031308f), rgb * 12.92f, 1.055f * pow(max0(rgb), 1.0f / 2.4f) - 0.055f);
					}
				}

				rgb = pow(max(rgb, vdfloat32x3::zero()), gamma) * params.mIntensityScale;

				*dst++	= packus8(permute<2,1,0>(rgb) * 255.0f) & 0xFFFFFF;

				*dst2++	= packus8(permute<2,1,0>(rgb * 127.0f + 64.0f)) & 0xFFFFFF;

				*dstu++ = packus8(permute<2,1,0>(rgb0) * 255.0f) & 0xFFFFFF;
			}

			// For monochrome modes, hues 2-15 will be the same as hue 1.
			if (useColorTint && hue == 1) {
				for(int i=0; i<14; ++i)
					memcpy(dst + i*16, dst - 16, sizeof(*dst) * 16);

				for(int i=0; i<14; ++i)
					memcpy(dst2 + i*16, dst2 - 16, sizeof(*dst2) * 16);

				for(int i=0; i<14; ++i)
					memcpy(dstu + i*16, dstu - 16, sizeof(*dstu) * 16);
				break;
			}
		}
	}

	// For VBXE, we need to push the uncorrected palette since it has to do the correction
	// on its side in order to handle RGB values written into palette registers. We also
	// inject Y into the alpha channel for use by the artifacting engine, since this makes
	// it substantially faster to do PAL chroma blending.
	for(uint32& v : mUncorrectedPalette) {
		// Y = floor(54*R + 183*G + 19*B + 0.5)
		v = (v & 0xFFFFFF) + (((v & 0xFF00FF) * 0x130036 + (v & 0xFF00) * 0xB700 + 0x800000) & 0xFF000000);
	}

	if (useMatrix)
		mColorMatchingMatrix = mx;
	else
		mColorMatchingMatrix.reset();

	if (useColorTint)
		mTintColor = tintColor;
	else
		mTintColor.reset();
}
