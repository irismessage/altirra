//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
#include "oshelper.h"
#include "resource.h"

///////////////////////////////////////////////////////////////////////////

class ATUIDialogDeviceParFileWriter final : public VDDialogFrameW32 {
public:
	ATUIDialogDeviceParFileWriter(ATPropertySet& props);

	bool OnLoaded() override;
	void OnDataExchange(bool write) override;
	bool OnCommand(uint32 id, uint32 extcode) override;

protected:
	uint32 mInhibitUpdateLocks;
	ATPropertySet& mProps;
};

ATUIDialogDeviceParFileWriter::ATUIDialogDeviceParFileWriter(ATPropertySet& props)
	: VDDialogFrameW32(IDD_DEVICE_PARFILEWRITER)
	, mInhibitUpdateLocks(0)
	, mProps(props)
{
}

bool ATUIDialogDeviceParFileWriter::OnLoaded() {
	ATUIEnableEditControlAutoComplete(GetControl(IDC_IDE_IMAGEPATH));

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogDeviceParFileWriter::OnDataExchange(bool write) {
	if (!write) {
		SetControlText(IDC_PATH, mProps.GetString("path"));
		CheckButton(IDC_TEXTMODE, mProps.GetBool("text_mode"));
	} else {
		const bool textMode = IsButtonChecked(IDC_TEXTMODE);

		VDStringW path;
		GetControlText(IDC_PATH, path);

		if (path.empty()) {
			FailValidation(IDC_PATH);
			return;
		}

		mProps.SetString("path", path.c_str());
		mProps.SetBool("text_mode", textMode);
	}
}

bool ATUIDialogDeviceParFileWriter::OnCommand(uint32 id, uint32 extcode) {
	switch(id) {
		case IDC_BROWSE:
			{
				VDStringW s(VDGetSaveFileName('prnt', (VDGUIHandle)mhdlg, L"Select file for parallel port output", L"All files\0*.*\0", nullptr));
				if (!s.empty())
					SetControlText(IDC_PATH, s.c_str());
			}
			return true;
	}

	return false;
}

bool ATUIConfDevParFileWriter(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDialogDeviceParFileWriter dlg(props);

	return dlg.ShowDialog(hParent) != 0;
}
