//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
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

#include <stdafx.h>
#include <at/atnativeui/dialog.h>
#include "resource.h"
#include "inputcontroller.h"

class ATUIDialogLightPen : public VDDialogFrameW32 {
public:
	ATUIDialogLightPen(ATLightPenPort *lpp);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);

	ATLightPenPort *mpLPP;
};

ATUIDialogLightPen::ATUIDialogLightPen(ATLightPenPort *lpp)
	: VDDialogFrameW32(IDD_LIGHTPEN)
	, mpLPP(lpp)
{
}

bool ATUIDialogLightPen::OnLoaded() {
	UDSetRange(IDC_GUN_HSPIN, -64, 64);
	UDSetRange(IDC_GUN_VSPIN, 64, -64);
	UDSetRange(IDC_PEN_HSPIN, -64, 64);
	UDSetRange(IDC_PEN_VSPIN, 64, -64);

	CBAddString(IDC_NOISE_MODE, L"None");
	CBAddString(IDC_NOISE_MODE, L"Low (CX-75 + 800)");
	CBAddString(IDC_NOISE_MODE, L"High (CX-75 + XL/XE)");

	OnDataExchange(false);
	SetFocusToControl(IDC_HVALUE);
	return true;
}

void ATUIDialogLightPen::OnDataExchange(bool write) {
	if (write) {
		const sint32 gunX = GetControlValueSint32(IDC_GUN_HVALUE);
		const sint32 gunY = GetControlValueSint32(IDC_GUN_VVALUE);
		const sint32 penX = GetControlValueSint32(IDC_PEN_HVALUE);
		const sint32 penY = GetControlValueSint32(IDC_PEN_VVALUE);

		if (!mbValidationFailed) {
			mpLPP->SetAdjust(false, { gunX, gunY });
			mpLPP->SetAdjust(true, { penX, penY });
		}

		int noiseModeIndex = CBGetSelectedIndex(IDC_NOISE_MODE);
		if (ATIsValidEnumValue<ATLightPenNoiseMode>(noiseModeIndex)) {
			mpLPP->SetNoiseMode((ATLightPenNoiseMode)noiseModeIndex);
		}
	} else {
		const auto [gunX, gunY] = mpLPP->GetAdjust(false);
		const auto [penX, penY] = mpLPP->GetAdjust(true);

		SetControlTextF(IDC_GUN_HVALUE, L"%d", gunX);
		SetControlTextF(IDC_GUN_VVALUE, L"%d", gunY);
		SetControlTextF(IDC_PEN_HVALUE, L"%d", penX);
		SetControlTextF(IDC_PEN_VVALUE, L"%d", penY);

		CBSetSelectedIndex(IDC_NOISE_MODE, (int)mpLPP->GetNoiseMode());
	}
}

void ATUIShowDialogLightPen(VDGUIHandle h, ATLightPenPort *lpp) {
	ATUIDialogLightPen dlg(lpp);

	dlg.ShowDialog(h);
}
