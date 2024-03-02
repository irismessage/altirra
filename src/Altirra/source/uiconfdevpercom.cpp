//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2017 Avery Lee
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

class ATUIDialogDevicePercom : public VDDialogFrameW32 {
public:
	ATUIDialogDevicePercom(ATPropertySet& props, bool atmode, bool atspdmode);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);

	ATPropertySet& mPropSet;
	bool mbATMode;
	bool mbATSPDMode;
	VDUIProxyComboBoxControl mComboFDCType;
	VDUIProxyComboBoxControl mComboDriveSelect;
	VDUIProxyComboBoxControl mComboDriveTypes[4];

	static const uint32 kDriveTypeIds[];
};

const uint32 ATUIDialogDevicePercom::kDriveTypeIds[]={
	IDC_DRIVETYPE1,
	IDC_DRIVETYPE2,
	IDC_DRIVETYPE3,
	IDC_DRIVETYPE4,
};

ATUIDialogDevicePercom::ATUIDialogDevicePercom(ATPropertySet& props, bool atmode, bool atspdmode)
	: VDDialogFrameW32(atmode ? IDD_DEVICE_PERCOMAT : IDD_DEVICE_PERCOMRFD)
	, mPropSet(props)
	, mbATMode(atmode)
	, mbATSPDMode(atspdmode)
{
}

bool ATUIDialogDevicePercom::OnLoaded() {
	if (mbATSPDMode) {
		AddProxy(&mComboFDCType, IDC_FDCTYPE);
		mComboFDCType.AddItem(L"1791 (side compare optional)");
		mComboFDCType.AddItem(L"1795 (side compare always on)");

		mComboFDCType.SetSelection(1);
	} else if (mbATMode) {
		AddProxy(&mComboFDCType, IDC_FDCTYPE);
		mComboFDCType.AddItem(L"1771+1791 (double density, side compare optional)");
		mComboFDCType.AddItem(L"1771+1795 (double density, side compare always on)");
		mComboFDCType.AddItem(L"1771 (single density only)");

		mComboFDCType.SetSelection(1);
	} else {
		AddProxy(&mComboDriveSelect, IDC_DRIVESELECT);

		VDStringW s;
		for(int i=1; i<=8; ++i) {
			s.sprintf(L"Drive %d (D%d:)", i, i);
			mComboDriveSelect.AddItem(s.c_str());
		}

		mComboDriveSelect.SetSelection(0);
	}

	for(size_t i=0; i<4; ++i) {
		auto& combo = mComboDriveTypes[i];
		AddProxy(&combo, kDriveTypeIds[i]);

		combo.AddItem(L"None");
		combo.AddItem(L"5.25\" (40 track)");
		combo.AddItem(L"5.25\" (80 track)");

		combo.SetSelection(0);
	}

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogDevicePercom::OnDataExchange(bool write) {
	if (write) {
		if (mbATMode) {
			const int fdcSel = mComboFDCType.GetSelection();

			if (mbATSPDMode) {
				mPropSet.SetBool("use1795", fdcSel != 0);
			} else {
				mPropSet.SetBool("use1795", fdcSel == 1);
				mPropSet.SetBool("ddcapable", fdcSel != 2);
			}
		} else
			mPropSet.SetUint32("id", mComboDriveSelect.GetSelection());

		VDStringA s;
		for(size_t i=0; i<4; ++i) {
			s.sprintf("drivetype%u", i);
			mPropSet.SetUint32(s.c_str(), mComboDriveTypes[i].GetSelection());
		}
	} else {
		VDStringA s;
		for(size_t i=0; i<4; ++i) {
			s.sprintf("drivetype%u", i);
			mComboDriveTypes[i].SetSelection(mPropSet.GetUint32(s.c_str(), i ? 0 : 1));
		}

		if (mbATMode) {
			bool use1795 = mPropSet.GetBool("use1795", false);

			if (mbATSPDMode)
				mComboFDCType.SetSelection(use1795 ? 1 : 0);
			else
				mComboFDCType.SetSelection(mPropSet.GetBool("ddcapable", true) ? use1795 ? 1 : 0 : 2);
		} else
			mComboDriveSelect.SetSelection(mPropSet.GetUint32("id", 0));
	}
}

bool ATUIConfDevPercomRFD(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDialogDevicePercom dlg(props, false, false);

	return dlg.ShowDialog(hParent) != 0;
}

bool ATUIConfDevPercomAT(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDialogDevicePercom dlg(props, true, false);

	return dlg.ShowDialog(hParent) != 0;
}

bool ATUIConfDevPercomATSPD(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDialogDevicePercom dlg(props, true, true);

	return dlg.ShowDialog(hParent) != 0;
}
