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
#include <windows.h>
#include <vd2/system/error.h>
#include <vd2/system/filesys.h>
#include <vd2/system/math.h>
#include <vd2/system/strutil.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/services.h>
#include <at/atnativeui/dialog.h>
#include "resource.h"
#include "idephysdisk.h"
#include "idevhdimage.h"
#include "oshelper.h"
#include <at/atnativeui/uiproxies.h>
#include <at/atcore/propertyset.h>

///////////////////////////////////////////////////////////////////////////

class ATUIDialogDeviceVirtFAT32 final : public VDDialogFrameW32 {
public:
	ATUIDialogDeviceVirtFAT32(ATPropertySet& props);
	~ATUIDialogDeviceVirtFAT32();

	bool OnLoaded() override;
	void OnDataExchange(bool write) override;
	bool OnCommand(uint32 id, uint32 extcode) override;

protected:
	uint32 mInhibitUpdateLocks;
	ATPropertySet& mProps;
};

ATUIDialogDeviceVirtFAT32::ATUIDialogDeviceVirtFAT32(ATPropertySet& props)
	: VDDialogFrameW32(IDD_DEVICE_HDVIRTFAT32)
	, mInhibitUpdateLocks(0)
	, mProps(props)
{
}

ATUIDialogDeviceVirtFAT32::~ATUIDialogDeviceVirtFAT32() {
}

bool ATUIDialogDeviceVirtFAT32::OnLoaded() {
	ATUIEnableEditControlAutoComplete(GetControl(IDC_PATH));

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogDeviceVirtFAT32::OnDataExchange(bool write) {
	if (!write) {
		SetControlText(IDC_PATH, mProps.GetString("path"));
	} else {
		VDStringW path;
		GetControlText(IDC_PATH, path);

		if (path.empty()) {
			FailValidation(IDC_PATH);
			return;
		}

		mProps.SetString("path", path.c_str());
	}
}

bool ATUIDialogDeviceVirtFAT32::OnCommand(uint32 id, uint32 extcode) {
	switch(id) {
		case IDC_BROWSE:
			if (const VDStringW& s = VDGetDirectory('vfat', (VDGUIHandle)mhdlg, L"Select path to map"); !s.empty())
				SetControlText(IDC_PATH, s.c_str());

			return true;
	}

	return false;
}

bool ATUIConfDevHDVirtFAT32(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDialogDeviceVirtFAT32 dlg(props);

	return dlg.ShowDialog(hParent) != 0;
}
