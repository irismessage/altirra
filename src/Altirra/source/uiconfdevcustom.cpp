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

class ATUIDialogDeviceCustom : public VDDialogFrameW32 {
public:
	ATUIDialogDeviceCustom(ATPropertySet& props);

protected:
	bool OnLoaded() override;
	void OnDataExchange(bool write) override;
	void OnBrowse();

	ATPropertySet& mPropSet;
	VDUIProxyEditControl mPathControl;
	VDUIProxyButtonControl mHotReloadControl;
	VDUIProxyButtonControl mBrowseControl;
};

ATUIDialogDeviceCustom::ATUIDialogDeviceCustom(ATPropertySet& props)
	: VDDialogFrameW32(IDD_DEVICE_CUSTOM)
	, mPropSet(props)
{
	mBrowseControl.SetOnClicked([this] { OnBrowse(); });
}

bool ATUIDialogDeviceCustom::OnLoaded() {
	SetCurrentSizeAsMinSize();

	AddProxy(&mPathControl, IDC_PATH);
	AddProxy(&mBrowseControl, IDC_BROWSE);
	AddProxy(&mHotReloadControl, IDC_HOTRELOAD);

	mResizer.Add(mPathControl.GetHandle(), mResizer.kTC);
	mResizer.Add(mBrowseControl.GetHandle(), mResizer.kTR);
	mResizer.Add(IDOK, mResizer.kBR);
	mResizer.Add(IDCANCEL, mResizer.kBR);

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogDeviceCustom::OnDataExchange(bool write) {
	if (write) {
		mPropSet.Clear();

		mPropSet.SetString("path", mPathControl.GetText().c_str());
		mPropSet.SetBool("hotreload", mHotReloadControl.GetChecked());
	} else {
		mPathControl.SetText(mPropSet.GetString("path", L""));
		mHotReloadControl.SetChecked(mPropSet.GetBool("hotreload", false));
	}
}

void ATUIDialogDeviceCustom::OnBrowse() {
	const VDStringW& path = VDGetLoadFileName('cudv', (VDGUIHandle)mhdlg, L"Select Custom Device Description",
		L"Altirra custom device desc. (*.atdevice)\0*.atdevice\0",
		L"atdevice");

	if (!path.empty())
		mPathControl.SetText(path.c_str());
}

///////////////////////////////////////////////////////////////////////////

bool ATUIConfDevCustom(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDialogDeviceCustom dlg(props);

	return dlg.ShowDialog(hParent) != 0;
}
