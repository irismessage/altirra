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

#ifndef f_AT_PALETTEGENERATOR_H
#define f_AT_PALETTEGENERATOR_H

#include <optional>
#include <vd2/system/vecmath.h>

struct ATColorParams;
enum class ATMonitorMode : uint8;

// ATColorPaletteGenerator
//
// Generates a representative palette for GTIA output. This generator is free
// threaded; it can only be run on one thread at a time but any thread is OK.
//
class ATColorPaletteGenerator {
public:
	void Generate(const ATColorParams& colorParams, ATMonitorMode monitorMode);

	// Final palette, as 24-bit RGB.
	uint32 mPalette[256];

	// Final palette, as 24-bit extended range RGB encoding [-0.5, 1.5). This
	// is used for efficient PAL blending.
	uint32 mSignedPalette[256];

	// Near-final palette without color correction. This is used by VBXE since
	// it has to deal with a user-modifiable palette and has to do correction
	// itself. The Y channel also contains the luminance, for accelerating PAL
	// blending when color correction is not active.
	uint32 mUncorrectedPalette[256];

	// If present, indicates a color correction matrix to apply in linear
	// RGB space.
	std::optional<vdfloat3x3> mColorMatchingMatrix;

	// If present, indicates that a monochrome display is active with the
	// given tint color.
	std::optional<nsVDVecMath::vdfloat32x3> mTintColor;
};

#endif
