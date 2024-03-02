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
#include <at/atnativeui/dialog.h>
#include "resource.h"
#include <windows.h>
#include "oshelper.h"

class ATUIDialogAbout : public VDDialogFrameW32 {
public:
	ATUIDialogAbout();
	~ATUIDialogAbout();

protected:
	bool OnLoaded();
	void OnSize();
	bool OnErase(VDZHDC hdc);

	VDDialogResizerW32 mResizer;
};

ATUIDialogAbout::ATUIDialogAbout()
	: VDDialogFrameW32(IDD_ABOUT)
{
}

ATUIDialogAbout::~ATUIDialogAbout() {
}

bool ATUIDialogAbout::OnLoaded() {
	vdfastvector<uint8> text;
	ATLoadMiscResource(IDR_ABOUT, text);

	text.push_back(0);
	text.push_back(0);

	SetControlText(IDC_EDIT, (const wchar_t *)text.data());

	mResizer.Init(mhdlg);
	mResizer.Add(IDC_EDIT, VDDialogResizerW32::kMC | VDDialogResizerW32::kAvoidFlicker);
	mResizer.Add(IDOK, VDDialogResizerW32::kAnchorX1_C | VDDialogResizerW32::kAnchorX2_C | VDDialogResizerW32::kB);

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogAbout::OnSize() {
	mResizer.Relayout();
}

bool ATUIDialogAbout::OnErase(VDZHDC hdc) {
	mResizer.Erase(&hdc);
	return true;
}

void ATUIShowDialogAbout(VDGUIHandle h) {
	ATUIDialogAbout().ShowDialog(h);
}
