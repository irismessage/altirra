//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2019 Avery Lee
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
#include <at/atcore/propertyset.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/uiproxies.h>
#include "resource.h"

class ATUIDialogDeviceAmdc : public VDDialogFrameW32 {
public:
	ATUIDialogDeviceAmdc(ATPropertySet& props);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);

	ATPropertySet& mPropSet;
	VDUIProxyComboBoxControl mComboDriveSelect;
	VDUIProxyComboBoxControl mComboDriveTypes[2];
	VDUIProxyButtonControl mDrive2PresentView;
	VDUIProxyButtonControl mSW1View;
	VDUIProxyButtonControl mSW2View;
	VDUIProxyButtonControl mSW3View;
	VDUIProxyButtonControl mSW4View;
	VDUIProxyButtonControl mSW7View;
	VDUIProxyButtonControl mSW8View;
	VDUIProxyButtonControl mJumperView;

	static const uint32 kDriveTypeIds[];
};

const uint32 ATUIDialogDeviceAmdc::kDriveTypeIds[]={
	IDC_DRIVETYPE3,
	IDC_DRIVETYPE4,
};

ATUIDialogDeviceAmdc::ATUIDialogDeviceAmdc(ATPropertySet& props)
	: VDDialogFrameW32(IDD_DEVICE_AMDC)
	, mPropSet(props)
{
}

bool ATUIDialogDeviceAmdc::OnLoaded() {
	AddProxy(&mComboDriveSelect, IDC_DRIVESELECT);
	AddProxy(&mDrive2PresentView, IDC_DRIVE2);
	AddProxy(&mSW1View, IDC_DEFAULTDD1);
	AddProxy(&mSW2View, IDC_DEFAULTDD2);
	AddProxy(&mSW3View, IDC_DEFAULTDD3);
	AddProxy(&mSW4View, IDC_DEFAULTDD4);
	AddProxy(&mSW7View, IDC_SW7);
	AddProxy(&mSW8View, IDC_SW8);
	AddProxy(&mJumperView, IDC_JUMPER);

	VDStringW s;
	for(int i=1; i<=8; ++i) {
		s.sprintf(L"Drive %d (D%d:)", i, i);
		mComboDriveSelect.AddItem(s.c_str());
	}

	mComboDriveSelect.SetSelection(0);

	for(size_t i=0; i<2; ++i) {
		auto& combo = mComboDriveTypes[i];
		AddProxy(&combo, kDriveTypeIds[i]);

		combo.AddItem(L"None");
		combo.AddItem(L"3\"/5.25\" (40 track)");
		combo.AddItem(L"3\"/5.25\" (80 track)");

		combo.SetSelection(0);
	}

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogDeviceAmdc::OnDataExchange(bool write) {
	static constexpr struct {
		VDUIProxyButtonControl ATUIDialogDeviceAmdc::*mpView;
		uint32 mBit;
	} kSwitchBits[] = {
		{ &ATUIDialogDeviceAmdc::mSW1View, 0x01 },
		{ &ATUIDialogDeviceAmdc::mSW2View, 0x02 },
		{ &ATUIDialogDeviceAmdc::mSW3View, 0x04 },
		{ &ATUIDialogDeviceAmdc::mSW4View, 0x08 },
		{ &ATUIDialogDeviceAmdc::mSW7View, 0x40 },
		{ &ATUIDialogDeviceAmdc::mSW8View, 0x80 },
		{ &ATUIDialogDeviceAmdc::mJumperView, 0x100 },
	};

	if (write) {
		uint32 switches = 0;

		switches += (mComboDriveSelect.GetSelection() & 3) << 4;

		for(const auto& sw : kSwitchBits)
			switches += (this->*sw.mpView).GetChecked() ? sw.mBit : 0;

		mPropSet.SetUint32("switches", switches);
		mPropSet.SetBool("drive2", mDrive2PresentView.GetChecked());

		VDStringA s;
		for(size_t i=0; i<2; ++i) {
			s.sprintf("extdrive%u", i);
			mPropSet.SetUint32(s.c_str(), mComboDriveTypes[i].GetSelection());
		}
	} else {
		VDStringA s;
		for(size_t i=0; i<2; ++i) {
			s.sprintf("extdrive%u", i);
			mComboDriveTypes[i].SetSelection(mPropSet.GetUint32(s.c_str(), 0));
		}

		// default to internal drives getting assigned first
		const uint32 switches = mPropSet.GetUint32("switches", 0x40);
		mComboDriveSelect.SetSelection((switches >> 4) & 3);

		for(const auto& sw : kSwitchBits)
			(this->*sw.mpView).SetChecked((switches & sw.mBit) != 0);

		mDrive2PresentView.SetChecked(mPropSet.GetBool("drive2", false));
	}
}

bool ATUIConfDevAMDC(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDialogDeviceAmdc dlg(props);

	return dlg.ShowDialog(hParent) != 0;
}
