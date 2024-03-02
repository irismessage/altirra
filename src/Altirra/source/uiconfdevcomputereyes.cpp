//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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

class ATUIDialogDeviceComputerEyes : public VDDialogFrameW32 {
public:
	ATUIDialogDeviceComputerEyes(ATPropertySet& props);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);

	void UpdateBrightnessLabel();

	ATPropertySet& mPropSet;
	VDUIProxyTrackbarControl mBrightnessView;
	VDUIProxyControl mBrightnessLabelView;
};

ATUIDialogDeviceComputerEyes::ATUIDialogDeviceComputerEyes(ATPropertySet& props)
	: VDDialogFrameW32(IDD_DEVICE_COMPUTEREYES)
	, mPropSet(props)
{
	mBrightnessView.SetOnValueChanged(
		[this](sint32 v, bool tracking) {
			UpdateBrightnessLabel();
		}
	);
}

bool ATUIDialogDeviceComputerEyes::OnLoaded() {
	AddProxy(&mBrightnessView, IDC_BRIGHTNESS);
	AddProxy(&mBrightnessLabelView, IDC_STATIC_BRIGHTNESS);

	mBrightnessView.SetRange(0, 100);

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogDeviceComputerEyes::OnDataExchange(bool write) {
	if (write) {
		mPropSet.Clear();

		mPropSet.SetUint32("brightness", mBrightnessView.GetValue());
	} else {
		mBrightnessView.SetValue(mPropSet.GetUint32("brightness", 50));
		UpdateBrightnessLabel();
	}
}

void ATUIDialogDeviceComputerEyes::UpdateBrightnessLabel() {
	VDStringW s;
	s.sprintf(L"%d", mBrightnessView.GetValue() - 50);
	mBrightnessLabelView.SetCaption(s.c_str());
}

bool ATUIConfDevComputerEyes(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDialogDeviceComputerEyes dlg(props);

	return dlg.ShowDialog(hParent) != 0;
}
