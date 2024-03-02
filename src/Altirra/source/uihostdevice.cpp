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
#include <vd2/system/error.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/services.h>
#include <at/atcore/device.h>
#include <at/atcore/propertyset.h>
#include <at/atnativeui/dialog.h>
#include "resource.h"
#include "hostdevice.h"
#include "oshelper.h"

VDStringW ATUIShowDialogBrowsePhysicalDisks(VDGUIHandle hParent);

namespace {
	const uint32 kPathIds[]={
		IDC_PATH1,
		IDC_PATH2,
		IDC_PATH3,
		IDC_PATH4,
	};
}

class ATUIDialogHostDevice : public VDDialogFrameW32 {
public:
	ATUIDialogHostDevice(ATPropertySet& pset);
	~ATUIDialogHostDevice();

	bool OnLoaded() override;
	void OnDataExchange(bool write) override;
	bool OnCommand(uint32 id, uint32 extcode) override;
	void Update(int id);

protected:
	ATPropertySet& mProps;
	uint32 mInhibitUpdateLocks;

	VDUIProxyComboBoxControl mLFNModeView;
};

ATUIDialogHostDevice::ATUIDialogHostDevice(ATPropertySet& props)
	: VDDialogFrameW32(IDD_HDEVICE)
	, mProps(props)
	, mInhibitUpdateLocks(0)
{
}

ATUIDialogHostDevice::~ATUIDialogHostDevice() {
}

bool ATUIDialogHostDevice::OnLoaded() {
	AddProxy(&mLFNModeView, IDC_LFNMODE);

	mLFNModeView.AddItem(L"8.3 only, truncate long names");
	mLFNModeView.AddItem(L"8.3 only, encode long names");
	mLFNModeView.AddItem(L"Use long file names");

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogHostDevice::OnDataExchange(bool write) {
	if (!write) {
		CheckButton(IDC_READONLY, mProps.GetBool("readonly", true));
		CheckButton(IDC_LOWERCASENAMES, mProps.GetBool("lowercase", true));
		CheckButton(IDC_INSTALLASDISK, mProps.GetBool("fakedisk", false));

		const bool encodeLFN = mProps.GetBool("encodelfn", true);
		const bool enableLFN = mProps.GetBool("longfilenames", false);

		mLFNModeView.SetSelection(enableLFN ? 2 : encodeLFN ? 1 : 0);

		for(int i=0; i<4; ++i)
			SetControlText(kPathIds[i], mProps.GetString(VDStringA().sprintf("path%d", i+1).c_str(), L""));
	} else {
		mProps.Clear();

		if (!IsButtonChecked(IDC_READONLY))
			mProps.SetBool("readonly", false);

		if (!IsButtonChecked(IDC_LOWERCASENAMES))
			mProps.SetBool("lowercase", false);

		if (IsButtonChecked(IDC_INSTALLASDISK))
			mProps.SetBool("fakedisk", true);

		int lfnMode = mLFNModeView.GetSelection();
		mProps.SetBool("encodelfn", lfnMode >= 1);
		mProps.SetBool("longfilenames", lfnMode >= 2);

		VDStringW path;

		for(int i=0; i<4; ++i) {
			GetControlText(kPathIds[i], path);

			if (!path.empty())
				mProps.SetString(VDStringA().sprintf("path%d", i+1).c_str(), path.c_str());
		}
	}
}

bool ATUIDialogHostDevice::OnCommand(uint32 id, uint32 extcode) {
	int index = 0;

	switch(id) {
		case IDC_BROWSE4:	++index;
		case IDC_BROWSE3:	++index;
		case IDC_BROWSE2:	++index;
		case IDC_BROWSE1:
			{
				VDStringW s(VDGetDirectory('host', (VDGUIHandle)mhdlg, L"Select base directory"));
				if (!s.empty())
					SetControlText(kPathIds[index], s.c_str());
			}
			return true;
	}

	return false;
}

bool ATUIConfDevHostFS(VDGUIHandle hParent, ATPropertySet& pset) {
	ATUIDialogHostDevice dlg(pset);

	return dlg.ShowDialog(hParent) != 0;
}
