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

#include <stdafx.h>
#include <at/atcore/propertyset.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/uiproxies.h>
#include "resource.h"

class ATUIDialogDeviceSoundBoard : public VDDialogFrameW32 {
public:
	ATUIDialogDeviceSoundBoard(ATPropertySet& props);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);

	ATPropertySet& mPropSet;
	VDUIProxyComboBoxControl mComboVersion;
	VDUIProxyComboBoxControl mComboBaseAddress;
};

ATUIDialogDeviceSoundBoard::ATUIDialogDeviceSoundBoard(ATPropertySet& props)
	: VDDialogFrameW32(IDD_DEVICE_SOUNDBOARD)
	, mPropSet(props)
{
}

bool ATUIDialogDeviceSoundBoard::OnLoaded() {
	AddProxy(&mComboVersion, IDC_COREVERSION);
	AddProxy(&mComboBaseAddress, IDC_BASEADDRESS);

	mComboVersion.AddItem(L"1.1 (VBXE-based)");
	mComboVersion.AddItem(L"1.2 (with multiplier)");
	mComboVersion.AddItem(L"2.0 Preview");

	for(const wchar_t *s : { L"$D280", L"$D2C0", L"$D600", L"$D700" } )
		mComboBaseAddress.AddItem(s);

	mComboBaseAddress.SetSelection(1);

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogDeviceSoundBoard::OnDataExchange(bool write) {
	static const uint32 kBaseAddresses[] = { 0xD280, 0xD2C0, 0xD600, 0xD700 };

	if (write) {
		mPropSet.Clear();

		int sel = mComboBaseAddress.GetSelection();

		if (sel >= 0 && (unsigned)sel < vdcountof(kBaseAddresses))
			mPropSet.SetUint32("base", kBaseAddresses[sel]);

		uint32 enc = 0;

		switch(mComboVersion.GetSelection()) {
			case 0:		enc = 110; break;
			case 1:
			default:	enc = 120; break;
			case 2:		enc = 200; break;
		}

		mPropSet.SetUint32("version", enc);
	} else {
		uint32 base;
		if (mPropSet.TryGetUint32("base", base)) {
			auto it = std::find(std::begin(kBaseAddresses), std::end(kBaseAddresses), base);

			if (it != std::end(kBaseAddresses))
				mComboBaseAddress.SetSelection((int)(it - std::begin(kBaseAddresses)));
		}

		int verIdx = 0;
		switch(mPropSet.GetUint32("version")) {
			case 110:	verIdx = 0; break;
			case 120:
			default:	verIdx = 1; break;
			case 200:	verIdx = 2; break;
		}

		mComboVersion.SetSelection(verIdx);
	}
}

bool ATUIConfDevSoundBoard(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDialogDeviceSoundBoard dlg(props);

	return dlg.ShowDialog(hParent) != 0;
}
