//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2024 Avery Lee
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

#include "stdafx.h"
#include <vd2/VDDisplay/display.h>
#include <at/atcore/configvar.h>
#include "simulator.h"
#include "gtia.h"

extern ATSimulator g_sim;

ATConfigVarRGBColor g_ATCVDisplayMonoColorWhite("display.mono_color_white", 0xFFFFFF,
	[] {
		g_sim.GetGTIA().RecomputePalette();
	}
);

void ATDispUpdateBloomCoeffs();

ATConfigVarFloat g_VDCVDisplayBloomCoeffWidthBase	("display.bloom.coeff_width_base",	VDDBloomV2Settings().mCoeffWidthBase, ATDispUpdateBloomCoeffs);
ATConfigVarFloat g_VDCVDisplayBloomCoeffWidthBaseSlope	("display.bloom.coeff_width_base_slope", VDDBloomV2Settings().mCoeffWidthBaseSlope, ATDispUpdateBloomCoeffs);
ATConfigVarFloat g_VDCVDisplayBloomCoeffWidthAdjustSlope("display.bloom.coeff_width_adjust_slope", VDDBloomV2Settings().mCoeffWidthAdjustSlope, ATDispUpdateBloomCoeffs);
ATConfigVarFloat g_VDCVDisplayBloomShoulderX		("display.bloom.shoulder_x",		VDDBloomV2Settings().mShoulderX, ATDispUpdateBloomCoeffs);
ATConfigVarFloat g_VDCVDisplayBloomShoulderY		("display.bloom.shoulder_y",		VDDBloomV2Settings().mShoulderY, ATDispUpdateBloomCoeffs);
ATConfigVarFloat g_VDCVDisplayBloomLimitX			("display.bloom.limit_x",			VDDBloomV2Settings().mLimitX, ATDispUpdateBloomCoeffs);
ATConfigVarFloat g_VDCVDisplayBloomLimitSlope		("display.bloom.limit_slope",		VDDBloomV2Settings().mLimitSlope, ATDispUpdateBloomCoeffs);

void ATDispUpdateBloomCoeffs() {
	VDDBloomV2Settings settings {
		.mCoeffWidthBase	= g_VDCVDisplayBloomCoeffWidthBase,
		.mCoeffWidthBaseSlope	= g_VDCVDisplayBloomCoeffWidthBaseSlope,
		.mCoeffWidthAdjustSlope	= g_VDCVDisplayBloomCoeffWidthAdjustSlope,
		.mShoulderX			= g_VDCVDisplayBloomShoulderX,
		.mShoulderY			= g_VDCVDisplayBloomShoulderY,
		.mLimitX			= g_VDCVDisplayBloomLimitX,
		.mLimitSlope		= g_VDCVDisplayBloomLimitSlope,
	};

	VDDSetBloomV2Settings(settings);
}

