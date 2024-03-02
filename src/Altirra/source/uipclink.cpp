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
#include <at/atui/dialog.h>
#include "resource.h"
#include "pclink.h"
#include "simulator.h"

class ATUIDialogPCLink : public VDDialogFrameW32 {
public:
	ATUIDialogPCLink(ATSimulator *sim);
	~ATUIDialogPCLink();

	bool OnLoaded();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void Update(int id);

protected:
	void UpdateEnables();

	ATSimulator *mpSim;
};

ATUIDialogPCLink::ATUIDialogPCLink(ATSimulator *sim)
	: VDDialogFrameW32(IDD_PCLINK)
	, mpSim(sim)
{
}

ATUIDialogPCLink::~ATUIDialogPCLink() {
}

bool ATUIDialogPCLink::OnLoaded() {
	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogPCLink::OnDataExchange(bool write) {
	if (!write) {
		IATPCLinkDevice *pclink = mpSim->GetPCLink();
		CheckButton(IDC_ENABLE, pclink != NULL);
		CheckButton(IDC_ALLOW_WRITES, pclink && !pclink->IsReadOnly());

		if (pclink)
			SetControlText(IDC_PATH, pclink->GetBasePath());

		UpdateEnables();
	} else {
		if (!IsButtonChecked(IDC_ENABLE)) {
			mpSim->SetPCLinkEnabled(false);
		} else {
			mpSim->SetPCLinkEnabled(true);

			IATPCLinkDevice *pclink = mpSim->GetPCLink();
			pclink->SetReadOnly(!IsButtonChecked(IDC_ALLOW_WRITES));
			VDStringW path;
			GetControlText(IDC_PATH, path);
			pclink->SetBasePath(path.c_str());
		}
	}
}

bool ATUIDialogPCLink::OnCommand(uint32 id, uint32 extcode) {
	int index = 0;

	switch(id) {
		case IDC_BROWSE:
			{
				VDStringW s(VDGetDirectory('pclk', (VDGUIHandle)mhdlg, L"Select base directory"));
				if (!s.empty())
					SetControlText(IDC_PATH, s.c_str());
			}
			return true;

		case IDC_ENABLE:
			if (extcode == BN_CLICKED)
				UpdateEnables();
			return true;
	}

	return false;
}

void ATUIDialogPCLink::UpdateEnables() {
	bool enable = IsButtonChecked(IDC_ENABLE);

	EnableControl(IDC_STATIC_HOSTPATH, enable);
	EnableControl(IDC_PATH, enable);
	EnableControl(IDC_ALLOW_WRITES, enable);
}

void ATUIShowPCLinkDialog(VDGUIHandle hParent, ATSimulator *sim) {
	ATUIDialogPCLink dlg(sim);

	dlg.ShowDialog(hParent);
}
