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
#include <vd2/Dita/services.h>
#include <at/atcore/propertyset.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/uiproxies.h>
#include "resource.h"

class ATUIDialogDeviceSIDE3 : public VDDialogFrameW32 {
public:
	ATUIDialogDeviceSIDE3(ATPropertySet& props);

protected:
	void OnDataExchange(bool write) override;

	ATPropertySet& mPropSet;
};

ATUIDialogDeviceSIDE3::ATUIDialogDeviceSIDE3(ATPropertySet& props)
	: VDDialogFrameW32(IDD_DEVICE_SIDE3)
	, mPropSet(props)
{
}

void ATUIDialogDeviceSIDE3::OnDataExchange(bool write) {
	if (write) {
		mPropSet.Clear();

		mPropSet.SetBool("led_enable", IsButtonChecked(IDC_ACTIVITYLED));
		mPropSet.SetBool("recovery", IsButtonChecked(IDC_RECOVERY));
	} else {
		CheckButton(IDC_ACTIVITYLED, mPropSet.GetBool("led_enable", true));
		CheckButton(IDC_RECOVERY, mPropSet.GetBool("recovery", false));
	}
}

///////////////////////////////////////////////////////////////////////////

bool ATUIConfDevSIDE3(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDialogDeviceSIDE3 dlg(props);

	return dlg.ShowDialog(hParent) != 0;
}
