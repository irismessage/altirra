//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2016 Avery Lee
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

class ATUIDialogDeviceCovox : public VDDialogFrameW32 {
public:
	ATUIDialogDeviceCovox(ATPropertySet& props);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);

	ATPropertySet& mPropSet;
	VDUIProxyComboBoxControl mComboAddress;
	VDUIProxyComboBoxControl mComboChannels;

	static const uint16 kRanges[][2];
};

const uint16 ATUIDialogDeviceCovox::kRanges[][2] = {
	{ 0xD100, 0x100 },
	{ 0xD280, 0x80 },
	{ 0xD500, 0x100 },
	{ 0xD600, 0x40 },
	{ 0xD600, 0x100 },		// also the default
	{ 0xD700, 0x100 },
};

ATUIDialogDeviceCovox::ATUIDialogDeviceCovox(ATPropertySet& props)
	: VDDialogFrameW32(IDD_DEVICE_COVOX)
	, mPropSet(props)
{
}

bool ATUIDialogDeviceCovox::OnLoaded() {
	AddProxy(&mComboAddress, IDC_ADDRESS);
	AddProxy(&mComboChannels, IDC_CHANNELS);

	for(auto [baseAddr, size] : kRanges)
		mComboAddress.AddItem(VDStringW().sprintf(L"$%04X-%04X", baseAddr, baseAddr + size - 1).c_str());

	mComboChannels.AddItem(L"1 channel (mono)");
	mComboChannels.AddItem(L"4 channels (stereo)");

	OnDataExchange(false);
	SetFocusToControl(IDC_ADDRESS);

	return true;
}

void ATUIDialogDeviceCovox::OnDataExchange(bool write) {
	if (write) {
		mPropSet.Clear();

		int sel = mComboAddress.GetSelection();
		if (sel >= 0 && sel < (int)vdcountof(kRanges)) {
			mPropSet.SetUint32("base", kRanges[sel][0]);
			mPropSet.SetUint32("size", kRanges[sel][1]);
		}

		mPropSet.SetUint32("channels", mComboChannels.GetSelection() > 0 ? 4 : 1);
	} else {
		const uint32 baseAddr = std::min<uint32>(mPropSet.GetUint32("base", 0xD600), 0xFFFF);
		const uint32 size = std::min<uint32>(mPropSet.GetUint32("size", 0), 0x0100);

		// find largest range with a matching base address and within the specified
		// size
		int idx = -1;
		uint32 bestSize = 0;
		uint32 maxSize = size ? size : 0x100;
		for(int i = 0; i < (int)vdcountof(kRanges); ++i) {
			if (kRanges[i][0] == baseAddr && kRanges[i][1] <= maxSize && kRanges[i][1] > bestSize) {
				idx = i;
				bestSize = kRanges[i][1];
			}
		}

		// default to $D600-D6FF if a range is specified that doesn't match
		if (idx < 0)
			idx = 4;

		mComboAddress.SetSelection(idx);

		mComboChannels.SetSelection(mPropSet.GetUint32("channels", 4) > 1 ? 1 : 0);
	}
}

bool ATUIConfDevCovox(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDialogDeviceCovox dlg(props);

	return dlg.ShowDialog(hParent) != 0;
}
