//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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
#include <vd2/system/error.h>
#include <vd2/Dita/services.h>
#include "Dialog.h"
#include "resource.h"
#include "uiproxies.h"
#include "uikeyboard.h"

class ATUIDialogKeyboardOptions : public VDDialogFrameW32 {
public:
	ATUIDialogKeyboardOptions(ATUIKeyboardOptions& opts);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);

	ATUIKeyboardOptions& mOpts;
	VDUIProxyComboBoxControl mArrowKeyMode;
};

ATUIDialogKeyboardOptions::ATUIDialogKeyboardOptions(ATUIKeyboardOptions& opts)
	: VDDialogFrameW32(IDD_KEYBOARD)
	, mOpts(opts)
{
}

bool ATUIDialogKeyboardOptions::OnLoaded() {
	AddProxy(&mArrowKeyMode, IDC_ARROWKEYMODE);

	mArrowKeyMode.AddItem(L"Arrows by default; Ctrl inverted");
	mArrowKeyMode.AddItem(L"Arrows by default; Ctrl/Shift states mapped directly");
	mArrowKeyMode.AddItem(L"Map host keys directly to -/=/+/*");

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogKeyboardOptions::OnDataExchange(bool write) {
	ExchangeControlValueBoolCheckbox(write, IDC_RESETSHIFT, mOpts.mbAllowShiftOnColdReset);
	ExchangeControlValueBoolCheckbox(write, IDC_ENABLE_FKEYS, mOpts.mbEnableFunctionKeys);

	if (write) {
		mOpts.mbRawKeys = IsButtonChecked(IDC_KPMODE_RAW);
		mOpts.mArrowKeyMode = (ATUIKeyboardOptions::ArrowKeyMode)mArrowKeyMode.GetSelection();
	} else {
		CheckButton(mOpts.mbRawKeys ? IDC_KPMODE_RAW : IDC_KPMODE_COOKED, true);
		mArrowKeyMode.SetSelection((int)mOpts.mArrowKeyMode);
	}
}

bool ATUIShowDialogKeyboardOptions(VDGUIHandle hParent, ATUIKeyboardOptions& opts) {
	ATUIKeyboardOptions tmp(opts);

	if (!ATUIDialogKeyboardOptions(tmp).ShowDialog(hParent))
		return false;

	opts = tmp;
	return true;
}
