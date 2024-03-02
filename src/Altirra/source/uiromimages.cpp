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
#include <vd2/system/filesys.h>
#include <vd2/system/math.h>
#include <vd2/Dita/services.h>
#include "Dialog.h"
#include "resource.h"
#include "simulator.h"

class ATUIDialogROMImages : public VDDialogFrameW32 {
public:
	ATUIDialogROMImages(ATSimulator& sim);

protected:
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void Browse(ATROMImage image);

	ATSimulator& mSim;

	static const uint32 kPathControlIds[kATROMImageCount];
};

const uint32 ATUIDialogROMImages::kPathControlIds[kATROMImageCount] = {
	IDC_PATH_OSA,
	IDC_PATH_OSB,
	IDC_PATH_XL,
	IDC_PATH_OTHER,
	IDC_PATH_5200,
	IDC_PATH_BASIC
};

ATUIDialogROMImages::ATUIDialogROMImages(ATSimulator& sim)
	: VDDialogFrameW32(IDD_ROM_IMAGES)
	, mSim(sim)
{
}

void ATUIDialogROMImages::OnDataExchange(bool write) {
	if (write) {
		VDStringW s;

		for(uint32 i=0; i<kATROMImageCount; ++i) {
			if (GetControlText(kPathControlIds[i], s))
				mSim.SetROMImagePath((ATROMImage)i, s.c_str());
		}
	} else {
		VDStringW s;

		for(uint32 i=0; i<kATROMImageCount; ++i) {
			mSim.GetROMImagePath((ATROMImage)i, s);

			SetControlText(kPathControlIds[i], s.c_str());
		}
	}
}

bool ATUIDialogROMImages::OnCommand(uint32 id, uint32 extcode) {
	switch(id) {
		case IDC_BROWSE_OSA:
			Browse(kATROMImage_OSA);
			break;

		case IDC_BROWSE_OSB:
			Browse(kATROMImage_OSB);
			break;

		case IDC_BROWSE_XL:
			Browse(kATROMImage_XL);
			break;

		case IDC_BROWSE_OTHER:
			Browse(kATROMImage_Other);
			break;

		case IDC_BROWSE_5200:
			Browse(kATROMImage_5200);
			break;

		case IDC_BROWSE_BASIC:
			Browse(kATROMImage_Basic);
			break;
	}

	return false;
}

void ATUIDialogROMImages::Browse(ATROMImage image) {
	const VDStringW& s = VDGetLoadFileName('ROMI', (VDGUIHandle)mhdlg, L"Browse for ROM image", L"ROM image (*.rom)\0*.rom\0All files\0*.*\0", NULL);

	if (!s.empty()) {
		const VDStringW& programPath = VDGetProgramPath();
		const VDStringW& relPath = VDFileGetRelativePath(programPath.c_str(), s.c_str(), false);

		if (!relPath.empty())
			SetControlText(kPathControlIds[image], relPath.c_str());
		else
			SetControlText(kPathControlIds[image], s.c_str());
	}
}

void ATUIShowDialogROMImages(VDGUIHandle hParent, ATSimulator& sim) {
	ATUIDialogROMImages dlg(sim);

	if (dlg.ShowDialog(hParent)) {
		sim.LoadROMs();
		sim.ColdReset();
	}
}
