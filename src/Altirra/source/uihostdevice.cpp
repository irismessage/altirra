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

#include "stdafx.h"
#include <windows.h>
#include <vd2/system/error.h>
#include <vd2/Dita/services.h>
#include "Dialog.h"
#include "resource.h"
#include "hostdevice.h"
#include "ide.h"
#include "simulator.h"
#include "oshelper.h"

#ifndef BCM_SETSHIELD
#define BCM_SETSHIELD	0x160C
#endif

extern ATSimulator g_sim;

VDStringW ATUIShowDialogBrowsePhysicalDisks(VDGUIHandle hParent);

namespace {
	const UINT kPathIds[]={
		IDC_PATH1,
		IDC_PATH2,
		IDC_PATH3,
		IDC_PATH4,
	};

	const UINT kBrowseIds[]={
		IDC_BROWSE1,
		IDC_BROWSE2,
		IDC_BROWSE3,
		IDC_BROWSE4,
	};

	const UINT kStaticIds[]={
		IDC_STATIC_HOSTPATH1,
		IDC_STATIC_HOSTPATH2,
		IDC_STATIC_HOSTPATH3,
		IDC_STATIC_HOSTPATH4,
	};
}

class ATUIDialogHostDevice : public VDDialogFrameW32 {
public:
	ATUIDialogHostDevice(IATHostDeviceEmulator *hd);
	~ATUIDialogHostDevice();

	bool OnLoaded();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void Update(int id);

protected:
	void UpdateEnables();

	IATHostDeviceEmulator *mpHostDevice;
	uint32 mInhibitUpdateLocks;
};

ATUIDialogHostDevice::ATUIDialogHostDevice(IATHostDeviceEmulator *hd)
	: VDDialogFrameW32(IDD_HDEVICE)
	, mpHostDevice(hd)
	, mInhibitUpdateLocks(0)
{
}

ATUIDialogHostDevice::~ATUIDialogHostDevice() {
}

bool ATUIDialogHostDevice::OnLoaded() {
	if (LOBYTE(LOWORD(GetVersion())) >= 6) {
		HWND hwndItem = GetDlgItem(mhdlg, IDC_IDE_DISKBROWSE);

		if (hwndItem)
			SendMessage(hwndItem, BCM_SETSHIELD, 0, TRUE);
	}

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogHostDevice::OnDataExchange(bool write) {
	if (!write) {
		CheckButton(IDC_ENABLE, mpHostDevice->IsEnabled());
		CheckButton(IDC_READONLY, mpHostDevice->IsReadOnly());
		CheckButton(IDC_BURSTIO, mpHostDevice->IsBurstIOEnabled());
		CheckButton(IDC_ENCODELONGNAMES, mpHostDevice->IsLongNameEncodingEnabled());

		for(int i=0; i<4; ++i)
			SetControlText(kPathIds[i], mpHostDevice->GetBasePath(i));

		UpdateEnables();
	} else {
		bool enable = IsButtonChecked(IDC_ENABLE);
		bool reset = false;

		if (mpHostDevice->IsEnabled() != enable) {
			mpHostDevice->SetEnabled(enable);
			reset = true;
		}

		mpHostDevice->SetReadOnly(IsButtonChecked(IDC_READONLY));
		mpHostDevice->SetBurstIOEnabled(IsButtonChecked(IDC_BURSTIO));
		mpHostDevice->SetLongNameEncodingEnabled(IsButtonChecked(IDC_ENCODELONGNAMES));

		VDStringW path;

		for(int i=0; i<4; ++i) {
			GetControlText(kPathIds[i], path);
			mpHostDevice->SetBasePath(i, path.c_str());
		}

		if (reset)
			g_sim.ColdReset();
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

		case IDC_ENABLE:
			if (extcode == BN_CLICKED)
				UpdateEnables();
			return true;
	}

	return false;
}

void ATUIDialogHostDevice::UpdateEnables() {
	bool henable = IsButtonChecked(IDC_ENABLE);

	EnableControl(IDC_READONLY, henable);
	EnableControl(IDC_BURSTIO, henable);
	EnableControl(IDC_ENCODELONGNAMES, henable);

	for(int i=0; i<4; ++i) {
		EnableControl(kStaticIds[i], henable);
		EnableControl(kPathIds[i], henable);
		EnableControl(kBrowseIds[i], henable);
	}
}

void ATUIShowHostDeviceDialog(VDGUIHandle hParent, IATHostDeviceEmulator *hd) {
	ATUIDialogHostDevice dlg(hd);

	dlg.ShowDialog(hParent);
}
