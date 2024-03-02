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
#include <at/atcore/propertyset.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/uiproxies.h>
#include "resource.h"

class ATUIDialogDeviceDiskDriveHappy810 : public VDDialogFrameW32 {
public:
	ATUIDialogDeviceDiskDriveHappy810(ATPropertySet& props);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	void UpdateEnables();
	void UpdateSpeedLabel();

	ATPropertySet& mPropSet;
	VDUIProxyButtonControl mAutospeedEnable;
	VDUIProxyControl mAutospeedLabel;
	VDUIProxyTrackbarControl mAutospeedRate;
	VDUIProxyComboBoxControl mComboDriveSelect;
};

ATUIDialogDeviceDiskDriveHappy810::ATUIDialogDeviceDiskDriveHappy810(ATPropertySet& props)
	: VDDialogFrameW32(IDD_DEVICE_HAPPY810)
	, mPropSet(props)
{
	mAutospeedEnable.SetOnClicked([this] { UpdateEnables(); });
	mAutospeedRate.SetOnValueChanged([this](sint32, bool) { UpdateSpeedLabel(); });
}

bool ATUIDialogDeviceDiskDriveHappy810::OnLoaded() {
	AddProxy(&mComboDriveSelect, IDC_DRIVESELECT);
	AddProxy(&mAutospeedEnable, IDC_AUTOSPEED);
	AddProxy(&mAutospeedLabel, IDC_STATIC_AUTOSPEEDRATE);
	AddProxy(&mAutospeedRate, IDC_AUTOSPEEDRATE);

	mAutospeedRate.SetRange(200, 400);
	mAutospeedRate.SetPageSize(10);

	VDStringW s;
	for(int i=1; i<=4; ++i) {
		s.sprintf(L"Drive %d (D%d:)", i, i);
		mComboDriveSelect.AddItem(s.c_str());
	}

	mComboDriveSelect.SetSelection(0);

	UpdateEnables();

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogDeviceDiskDriveHappy810::OnDataExchange(bool write) {
	if (write) {
		mPropSet.SetUint32("id", mComboDriveSelect.GetSelection());
		mPropSet.SetBool("autospeed", mAutospeedEnable.GetChecked());
		mPropSet.SetFloat("autospeedrate", (float)mAutospeedRate.GetValue());
	} else {
		mComboDriveSelect.SetSelection(mPropSet.GetUint32("id", 0));
		mAutospeedEnable.SetChecked(mPropSet.GetBool("autospeed"));
		mAutospeedRate.SetValue(std::clamp<float>(mPropSet.GetFloat("autospeedrate", 266.0f), 200.0f, 400.0f));
		UpdateEnables();
		UpdateSpeedLabel();
	}
}

void ATUIDialogDeviceDiskDriveHappy810::UpdateEnables() {
	mAutospeedLabel.SetEnabled(mAutospeedEnable.GetChecked());
}

void ATUIDialogDeviceDiskDriveHappy810::UpdateSpeedLabel() {
	VDStringW s;
	s.sprintf(L"%d RPM", mAutospeedRate.GetValue());
	mAutospeedLabel.SetCaption(s.c_str());
}

bool ATUIConfDevHappy810(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDialogDeviceDiskDriveHappy810 dlg(props);

	return dlg.ShowDialog(hParent) != 0;
}
