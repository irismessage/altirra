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
#include <at/atnativeui/dialog.h>
#include "resource.h"
#include <at/atnativeui/uiproxies.h>
#include "uikeyboard.h"

bool ATUIShowDialogKeyboardCustomize(VDGUIHandle hParent);

class ATUIDialogKeyboardOptions : public VDDialogFrameW32 {
public:
	ATUIDialogKeyboardOptions(ATUIKeyboardOptions& opts);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	void OnKeyModeChanged(VDUIProxyComboBoxControl *, int);
	void OnLayoutModeChanged(VDUIProxyComboBoxControl *, int);
	void OnCopyLayout();
	void OnCustomizeLayout();
	void UpdateEnables();
	void UpdateKeyModeDesc();
	ATUIKeyboardOptions GetOptions() const;

	ATUIKeyboardOptions& mOpts;
	VDUIProxyComboBoxControl mKeyMode;
	VDUIProxyComboBoxControl mArrowKeyMode;
	VDUIProxyComboBoxControl mLayoutMode;
	VDUIProxyButtonControl mCopyButton;
	VDUIProxyButtonControl mCustomizeButton;
	VDDelegate mDelKeyModeChanged;
	VDDelegate mDelLayoutModeChanged;

	static const wchar_t *const kKeyModeDescriptions[];
};

const wchar_t *const ATUIDialogKeyboardOptions::kKeyModeDescriptions[] = {
	L"Characters are translated to key presses. This lets key repeat run at host rates and allows typing even at warp speed.",
	L"Raw keys are sent to the emulated hardware. This allow software to detect held keys, but can cause unintended repeats at warp speed and may not work well with localized keyboard layouts.",
	L"Full raw keyboard emulation including keyboard scan timing. This is the most accurate mode but limits typing speed."
};

ATUIDialogKeyboardOptions::ATUIDialogKeyboardOptions(ATUIKeyboardOptions& opts)
	: VDDialogFrameW32(IDD_KEYBOARD)
	, mOpts(opts)
{
	mKeyMode.OnSelectionChanged() += mDelKeyModeChanged.Bind(this, &ATUIDialogKeyboardOptions::OnKeyModeChanged);
	mLayoutMode.OnSelectionChanged() += mDelLayoutModeChanged.Bind(this, &ATUIDialogKeyboardOptions::OnLayoutModeChanged);
	mCopyButton.SetOnClicked([this](VDUIProxyButtonControl*) { OnCopyLayout(); });
	mCustomizeButton.SetOnClicked([this](VDUIProxyButtonControl*) { OnCustomizeLayout(); });
}

bool ATUIDialogKeyboardOptions::OnLoaded() {
	AddProxy(&mKeyMode, IDC_KEYMODE);
	AddProxy(&mArrowKeyMode, IDC_ARROWKEYMODE);
	AddProxy(&mLayoutMode, IDC_LAYOUT);
	AddProxy(&mCopyButton, IDC_COPY_TO_CUSTOM);
	AddProxy(&mCustomizeButton, IDC_CUSTOMIZE);

	mKeyMode.AddItem(L"Cooked keys");
	mKeyMode.AddItem(L"Raw keys");
	mKeyMode.AddItem(L"Full raw keyboard scan");

	mArrowKeyMode.AddItem(L"Arrows by default; Ctrl inverted");
	mArrowKeyMode.AddItem(L"Arrows by default; Ctrl/Shift states mapped directly");
	mArrowKeyMode.AddItem(L"Map host keys directly to -/=/+/*");

	mLayoutMode.AddItem(L"Natural: Map host keys by typed character");
	mLayoutMode.AddItem(L"Direct: Map host keys by location");
	mLayoutMode.AddItem(L"Custom layout");

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogKeyboardOptions::OnDataExchange(bool write) {
	if (write) {
		mOpts = GetOptions();
	} else {
		ExchangeControlValueBoolCheckbox(write, IDC_RESETSHIFT, mOpts.mbAllowShiftOnColdReset);
		ExchangeControlValueBoolCheckbox(write, IDC_ENABLE_FKEYS, mOpts.mbEnableFunctionKeys);

		mKeyMode.SetSelection(mOpts.mbRawKeys ? mOpts.mbFullRawKeys ? 2 : 1 : 0);
		mArrowKeyMode.SetSelection((int)mOpts.mArrowKeyMode);
		mLayoutMode.SetSelection((int)mOpts.mLayoutMode);

		UpdateEnables();
		UpdateKeyModeDesc();
	}
}

void ATUIDialogKeyboardOptions::OnKeyModeChanged(VDUIProxyComboBoxControl *, int) {
	UpdateKeyModeDesc();
}

void ATUIDialogKeyboardOptions::OnLayoutModeChanged(VDUIProxyComboBoxControl *, int) {
	UpdateEnables();
}

void ATUIDialogKeyboardOptions::OnCopyLayout() {
	const auto opts = GetOptions();

	if (opts.mLayoutMode == opts.kLM_Custom)
		return;

	vdfastvector<uint32> mappings;
	ATUIGetDefaultKeyMap(opts, mappings);
	ATUISetCustomKeyMap(mappings.data(), mappings.size());

	mLayoutMode.SetSelection(ATUIKeyboardOptions::kLM_Custom);
	UpdateEnables();
}

void ATUIDialogKeyboardOptions::OnCustomizeLayout() {
	if (mLayoutMode.GetSelection() != ATUIKeyboardOptions::kLM_Custom)
		return;

	ATUIShowDialogKeyboardCustomize((VDGUIHandle)mhdlg);
}

void ATUIDialogKeyboardOptions::UpdateEnables() {
	const bool customLayout = (mLayoutMode.GetSelection() == ATUIKeyboardOptions::kLM_Custom);

	EnableControl(IDC_COPY_TO_CUSTOM, !customLayout);
	EnableControl(IDC_CUSTOMIZE, customLayout);
	EnableControl(IDC_STATIC_ARROWKEYMODE, !customLayout);
	EnableControl(IDC_ARROWKEYMODE, !customLayout);
}

void ATUIDialogKeyboardOptions::UpdateKeyModeDesc() {
	int mode = mKeyMode.GetSelection();

	if ((unsigned)mode < vdcountof(kKeyModeDescriptions)) {
		SetControlText(IDC_STATIC_KEYMODE, kKeyModeDescriptions[mode]);
	} else
		SetControlText(IDC_STATIC_KEYMODE, L"");
}

ATUIKeyboardOptions ATUIDialogKeyboardOptions::GetOptions() const {
	ATUIKeyboardOptions opts;
	opts.mbAllowShiftOnColdReset = IsButtonChecked(IDC_RESETSHIFT);
	opts.mbEnableFunctionKeys = IsButtonChecked(IDC_ENABLE_FKEYS);

	int keyMode = mKeyMode.GetSelection();
	opts.mbRawKeys = keyMode > 0;
	opts.mbFullRawKeys = keyMode > 1;

	opts.mArrowKeyMode = (ATUIKeyboardOptions::ArrowKeyMode)mArrowKeyMode.GetSelection();
	opts.mLayoutMode = (ATUIKeyboardOptions::LayoutMode)mLayoutMode.GetSelection();

	return opts;
}

bool ATUIShowDialogKeyboardOptions(VDGUIHandle hParent, ATUIKeyboardOptions& opts) {
	ATUIKeyboardOptions tmp(opts);

	if (!ATUIDialogKeyboardOptions(tmp).ShowDialog(hParent))
		return false;

	opts = tmp;
	return true;
}
