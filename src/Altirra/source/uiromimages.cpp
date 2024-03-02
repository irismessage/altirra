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
#include <vd2/system/filesys.h>
#include <vd2/system/math.h>
#include <vd2/Dita/services.h>
#include <at/atui/dialog.h>
#include "resource.h"
#include "oshelper.h"
#include "simulator.h"

class ATUIDialogROMImages : public VDDialogFrameW32 {
public:
	ATUIDialogROMImages(ATSimulator& sim);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void Browse();

	void OnSelChanged(VDUIProxyListView *sender, int idx);

	struct ROMImageItem : public vdrefcounted<IVDUIListViewVirtualItem> {
		ROMImageItem(ATROMImage image)
			: mImage(image)
		{
		}

		void GetText(int subItem, VDStringW& s) const;

		const ATROMImage mImage;
		VDStringW mPath;
	};

	ATSimulator& mSim;
	int mEditLockCount;

	vdrefptr<ROMImageItem> mItems[kATROMImageCount];

	VDUIProxyListView mListView;

	VDDelegate mDelSelChanged;

	static const wchar_t *const kPathNames[kATROMImageCount];
	static const ATROMImage kPathIds[kATROMImageCount];
};

const wchar_t *const ATUIDialogROMImages::kPathNames[kATROMImageCount] = {
	L"OS-A",
	L"OS-B",
	L"XL/XE",
	L"1200XL",
	L"Other",
	L"5200",
	L"XEGS kernel",
	L"BASIC",
	L"XEGS game",
	L"KMK/JZ IDE",
	L"IDEPlus 2.0 (Main)",
	L"IDEPlus 2.0 (SDX)",
	L"SIDE (SDX)",
	L"MyIDE II",
	L"Ultimate1MB",
};

const ATROMImage ATUIDialogROMImages::kPathIds[kATROMImageCount] = {
	kATROMImage_OSA,
	kATROMImage_OSB,
	kATROMImage_XL,
	kATROMImage_1200XL,
	kATROMImage_Other,
	kATROMImage_5200,
	kATROMImage_XEGS,
	kATROMImage_Basic,
	kATROMImage_Game,
	kATROMImage_KMKJZIDE,
	kATROMImage_KMKJZIDEV2,
	kATROMImage_KMKJZIDEV2_SDX,
	kATROMImage_SIDE_SDX,
	kATROMImage_MyIDEII,
	kATROMImage_Ultimate1MB,
};

void ATUIDialogROMImages::ROMImageItem::GetText(int subItem, VDStringW& s) const {
	if (subItem)
		s = mPath;
	else
		s = kPathNames[mImage];
}

ATUIDialogROMImages::ATUIDialogROMImages(ATSimulator& sim)
	: VDDialogFrameW32(IDD_ROM_IMAGES)
	, mSim(sim)
	, mEditLockCount(0)
{
}

bool ATUIDialogROMImages::OnLoaded() {
	AddProxy(&mListView, IDC_LIST);

	mListView.SetRedraw(false);
	mListView.InsertColumn(0, L"ROM Name", 0);
	mListView.InsertColumn(1, L"Path", 0);
	mListView.SetFullRowSelectEnabled(true);

	for(int i=0; i<kATROMImageCount; ++i) {
		mItems[i] = new ROMImageItem((ATROMImage)i);

		mListView.InsertVirtualItem(i, mItems[i]);
	}

	OnDataExchange(false);

	mListView.AutoSizeColumns(true);
	mListView.SetRedraw(true);

	ATUIEnableEditControlAutoComplete(GetControl(IDC_PATH));

	EnableControl(IDC_PATH, false);
	EnableControl(IDC_BROWSE, false);
	mListView.OnItemSelectionChanged() += mDelSelChanged.Bind(this, &ATUIDialogROMImages::OnSelChanged);

	SetFocusToControl(IDC_LIST);
	return true;
}

void ATUIDialogROMImages::OnDataExchange(bool write) {
	if (write) {
		for(uint32 i=0; i<kATROMImageCount; ++i)
			mSim.SetROMImagePath(kPathIds[i], mItems[i]->mPath.c_str());
	} else {
		for(uint32 i=0; i<kATROMImageCount; ++i)
			mSim.GetROMImagePath(kPathIds[i], mItems[i]->mPath);

		mListView.RefreshAllItems();
	}
}

bool ATUIDialogROMImages::OnCommand(uint32 id, uint32 extcode) {
	switch(id) {
		case IDC_BROWSE:
			Browse();
			break;

		case IDC_PATH:
			if (extcode == EN_CHANGE) {
				VDStringW s;
				if (GetControlText(IDC_PATH, s)) {
					ROMImageItem *item = static_cast<ROMImageItem *>(mListView.GetSelectedVirtualItem());

					if (item) {
						item->mPath = s;
						mListView.RefreshItem(item->mImage);
					}
				}
			}

			break;
	}

	return false;
}

void ATUIDialogROMImages::Browse() {
	ROMImageItem *item = static_cast<ROMImageItem *>(mListView.GetSelectedVirtualItem());
	if (!item)
		return;

	const VDStringW& s = VDGetLoadFileName('ROMI', (VDGUIHandle)mhdlg, L"Browse for ROM image", L"ROM image (*.rom)\0*.rom\0All files\0*.*\0", NULL);

	if (!s.empty()) {
		const VDStringW& programPath = VDGetProgramPath();
		const VDStringW& relPath = VDFileGetRelativePath(programPath.c_str(), s.c_str(), false);
		const VDStringW *path = relPath.empty() ? &s : &relPath;

		SetControlText(IDC_PATH, path->c_str());
		
		item->mPath = *path;

		mListView.RefreshItem(item->mImage);
	}
}

void ATUIDialogROMImages::OnSelChanged(VDUIProxyListView *sender, int idx) {
	if (idx >= 0 && idx < kATROMImageCount) {
		++mEditLockCount;
		SetControlText(IDC_PATH, mItems[idx]->mPath.c_str());
		--mEditLockCount;
		EnableControl(IDC_PATH, true);
		EnableControl(IDC_BROWSE, true);
	} else {
		EnableControl(IDC_PATH, false);
		EnableControl(IDC_BROWSE, false);
	}
}

void ATUIShowDialogROMImages(VDGUIHandle hParent, ATSimulator& sim) {
	ATUIDialogROMImages dlg(sim);

	if (dlg.ShowDialog(hParent)) {
		sim.LoadROMs();
		sim.ColdReset();
	}
}
