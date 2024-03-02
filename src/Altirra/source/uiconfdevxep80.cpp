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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <at/atcore/propertyset.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/uiproxies.h>
#include "resource.h"

class ATUIDialogDeviceXEP80 : public VDDialogFrameW32 {
public:
	ATUIDialogDeviceXEP80(ATPropertySet& props);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);

	ATPropertySet& mPropSet;
	VDUIProxyComboBoxControl mComboJoyPort;
};

ATUIDialogDeviceXEP80::ATUIDialogDeviceXEP80(ATPropertySet& props)
	: VDDialogFrameW32(IDD_DEVICE_XEP80)
	, mPropSet(props)
{
}

bool ATUIDialogDeviceXEP80::OnLoaded() {
	AddProxy(&mComboJoyPort, IDC_PORT);

	mComboJoyPort.AddItem(L"Port 1");
	mComboJoyPort.AddItem(L"Port 2 (default)");
	mComboJoyPort.AddItem(L"Port 3 (400/800 only)");
	mComboJoyPort.AddItem(L"Port 4 (400/800 only)");

	mComboJoyPort.SetSelection(1);

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogDeviceXEP80::OnDataExchange(bool write) {
	if (write) {
		mPropSet.Clear();

		if (mComboJoyPort.GetSelection() >= 0)
			mPropSet.SetUint32("port", (mComboJoyPort.GetSelection() & 3) + 1);
	} else {
		// default to port 2
		mComboJoyPort.SetSelection(std::clamp<uint32>(mPropSet.GetUint32("port", 2) - 1, 0, 3));
	}
}

bool ATUIConfDevXEP80(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDialogDeviceXEP80 dlg(props);

	return dlg.ShowDialog(hParent) != 0;
}
