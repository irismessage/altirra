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

#ifndef f_VD2_VDDISPLAY_INTERNAL_BLOOM_H
#define f_VD2_VDDISPLAY_INTERNAL_BLOOM_H

#include <vd2/system/vectors.h>

struct VDDBloomV2Settings;

extern VDDBloomV2Settings g_VDDispBloomV2Settings;
extern uint32 g_VDDispBloomCoeffsChanged;

struct VDDBloomV2ControlParams {
	bool mbRenderLinear;
	float mBaseRadius;
	float mAdjustRadius;
	float mDirectIntensity;
	float mIndirectIntensity;
};

struct VDDBloomV2RenderParams {
	vdfloat2 mPassBlendFactors[6];
	vdfloat4 mShoulder;
	vdfloat4 mThresholds;
};

VDDBloomV2RenderParams VDDComputeBloomV2Parameters(const VDDBloomV2ControlParams& controlParams);

#endif
