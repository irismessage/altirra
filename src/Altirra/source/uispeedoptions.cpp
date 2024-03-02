//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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
#include <at/atui/dialog.h>
#include "uitypes.h"
#include "resource.h"

extern ATFrameRateMode g_frameRateMode;
extern float g_speedModifier;

void ATUIUpdateSpeedTiming();

class ATUIDialogSpeedOptions : public VDDialogFrameW32 {
public:
	ATUIDialogSpeedOptions();

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	void OnHScroll(uint32 id, int code);

	void UpdateSpeedLabel();

	int GetAdjustedSpeedTicks();
};

ATUIDialogSpeedOptions::ATUIDialogSpeedOptions()
	: VDDialogFrameW32(IDD_SPEED)
{
}

bool ATUIDialogSpeedOptions::OnLoaded() {
	TBSetRange(IDC_SPEED_ADJUST, 1, 550);

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogSpeedOptions::OnDataExchange(bool write) {
	if (write) {
		if (IsButtonChecked(IDC_RATE_HARDWARE))
			g_frameRateMode = kATFrameRateMode_Hardware;
		else if (IsButtonChecked(IDC_RATE_BROADCAST))
			g_frameRateMode = kATFrameRateMode_Broadcast;
		else if (IsButtonChecked(IDC_RATE_INTEGRAL))
			g_frameRateMode = kATFrameRateMode_Integral;

		g_speedModifier = (float)GetAdjustedSpeedTicks() / 100.0f - 1.0f;

		ATUIUpdateSpeedTiming();
	} else {
		CheckButton(IDC_RATE_HARDWARE, g_frameRateMode == kATFrameRateMode_Hardware);
		CheckButton(IDC_RATE_BROADCAST, g_frameRateMode == kATFrameRateMode_Broadcast);
		CheckButton(IDC_RATE_INTEGRAL, g_frameRateMode == kATFrameRateMode_Integral);

		int rawTicks = VDRoundToInt((g_speedModifier + 1.0f) * 100.0f);

		if (rawTicks >= 100) {
			if (rawTicks > 100)
				rawTicks += 50;
			else
				rawTicks += 25;
		}

		TBSetValue(IDC_SPEED_ADJUST, rawTicks);

		UpdateSpeedLabel();
	}
}

void ATUIDialogSpeedOptions::OnHScroll(uint32 id, int code) {
	UpdateSpeedLabel();
}

void ATUIDialogSpeedOptions::UpdateSpeedLabel() {
	SetControlTextF(IDC_STATIC_SPEED_ADJUST, L"%d%%", GetAdjustedSpeedTicks());
}

int ATUIDialogSpeedOptions::GetAdjustedSpeedTicks() {
	int rawTicks = TBGetValue(IDC_SPEED_ADJUST);

	if (rawTicks >= 100) {
		if (rawTicks >= 150)
			rawTicks -= 50;
		else
			rawTicks = 100;
	}

	return rawTicks;
}

void ATUIShowDialogSpeedOptions(VDGUIHandle parent) {
	ATUIDialogSpeedOptions dlg;

	dlg.ShowDialog(parent);
}
