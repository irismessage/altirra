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
#include "Dialog.h"
#include "resource.h"
#include "disk.h"
#include "simulator.h"

extern ATSimulator g_sim;

class ATNewDiskDialog : public VDDialogFrameW32 {
public:
	ATNewDiskDialog();
	~ATNewDiskDialog();

	uint32 GetSectorCount() const { return mSectorCount; }
	uint32 GetBootSectorCount() const { return mBootSectorCount; }
	uint32 GetSectorSize() const { return mSectorSize; }

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void UpdateEnables();

	enum Format {
		kFormatSingle,
		kFormatMedium,
		kFormatDouble,
		kFormatCustom
	};

	Format	mFormat;
	uint32	mSectorCount;
	uint32	mBootSectorCount;
	uint32	mSectorSize;
};

ATNewDiskDialog::ATNewDiskDialog()
	: VDDialogFrameW32(IDD_CREATE_DISK)
	, mFormat(kFormatSingle)
	, mSectorCount(720)
	, mBootSectorCount(3)
	, mSectorSize(128)
{
}

ATNewDiskDialog::~ATNewDiskDialog() {
}

bool ATNewDiskDialog::OnLoaded() {
	CBAddString(IDC_FORMAT, L"Single density (720 sectors, 128 bytes/sector)");
	CBAddString(IDC_FORMAT, L"Medium density (1040 sectors, 128 bytes/sector)");
	CBAddString(IDC_FORMAT, L"Double density (720 sectors, 256 bytes/sector)");
	CBAddString(IDC_FORMAT, L"Custom");

	return VDDialogFrameW32::OnLoaded();
}

void ATNewDiskDialog::OnDataExchange(bool write) {
	ExchangeControlValueUint32(write, IDC_BOOT_SECTOR_COUNT, mBootSectorCount, 0, 255);
	ExchangeControlValueUint32(write, IDC_SECTOR_COUNT, mSectorCount, mBootSectorCount, 65535);

	if (write) {
		mSectorSize = 128;
		if (IsButtonChecked(IDC_SECTOR_SIZE_256))
			mSectorSize = 256;
		else if (IsButtonChecked(IDC_SECTOR_SIZE_512))
			mSectorSize = 512;
	} else {
		CheckButton(IDC_SECTOR_SIZE_128, mSectorSize == 128);
		CheckButton(IDC_SECTOR_SIZE_256, mSectorSize == 256);
		CheckButton(IDC_SECTOR_SIZE_512, mSectorSize == 512);
		CBSetSelectedIndex(IDC_FORMAT, (int)mFormat);
		UpdateEnables();
	}
}

bool ATNewDiskDialog::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_FORMAT && extcode == CBN_SELCHANGE) {
		Format format = (Format)CBGetSelectedIndex(IDC_FORMAT);

		if (mFormat != format) {
			mFormat = format;
			UpdateEnables();

			switch(format) {
				case kFormatSingle:
					mSectorCount = 720;
					mBootSectorCount = 3;
					mSectorSize = 128;
					OnDataExchange(false);
					break;

				case kFormatMedium:
					mSectorCount = 1040;
					mBootSectorCount = 3;
					mSectorSize = 128;
					OnDataExchange(false);
					break;

				case kFormatDouble:
					mSectorCount = 720;
					mBootSectorCount = 3;
					mSectorSize = 256;
					OnDataExchange(false);
					break;
			}
		}
	}

	return false;
}

void ATNewDiskDialog::UpdateEnables() {
	bool custom = (CBGetSelectedIndex(IDC_FORMAT) == kFormatCustom);

	EnableControl(IDC_SECTOR_SIZE_128, custom);
	EnableControl(IDC_SECTOR_SIZE_256, custom);
	EnableControl(IDC_SECTOR_SIZE_512, custom);
	EnableControl(IDC_SECTOR_COUNT, custom);
	EnableControl(IDC_BOOT_SECTOR_COUNT, custom);
}

///////////////////////////////////////////////////////////////////////////////

class ATDiskDriveDialog : public VDDialogFrameW32 {
public:
	ATDiskDriveDialog();
	~ATDiskDriveDialog();

	bool OnLoaded();
	void OnDestroy();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void Update(int id);

protected:
	VDZINT_PTR DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam);
	
	bool mbHighDrives;
	HBRUSH mDirtyDiskBrush;
	COLORREF mDirtyDiskColor;
};

ATDiskDriveDialog::ATDiskDriveDialog()
	: VDDialogFrameW32(IDD_DISK_DRIVES)
	, mbHighDrives(false)
	, mDirtyDiskBrush(NULL)
{
}

ATDiskDriveDialog::~ATDiskDriveDialog() {
}

namespace {
	const uint32 kDriveLabelID[]={
		IDC_STATIC_D1,
		IDC_STATIC_D2,
		IDC_STATIC_D3,
		IDC_STATIC_D4,
		IDC_STATIC_D5,
		IDC_STATIC_D6,
		IDC_STATIC_D7,
		IDC_STATIC_D8,
	};

	const uint32 kDiskPathID[]={
		IDC_DISKPATH1,
		IDC_DISKPATH2,
		IDC_DISKPATH3,
		IDC_DISKPATH4,
		IDC_DISKPATH5,
		IDC_DISKPATH6,
		IDC_DISKPATH7,
		IDC_DISKPATH8
	};

	const uint32 kWriteModeID[]={
		IDC_WRITEMODE1,
		IDC_WRITEMODE2,
		IDC_WRITEMODE3,
		IDC_WRITEMODE4,
		IDC_WRITEMODE5,
		IDC_WRITEMODE6,
		IDC_WRITEMODE7,
		IDC_WRITEMODE8
	};
}

bool ATDiskDriveDialog::OnLoaded() {
	if (!mDirtyDiskBrush) {
		DWORD c = GetSysColor(COLOR_3DFACE);

		// redden the color
		uint32 d = RGB(255, 128, 64);

		c = (c|d) - (((c^d) & 0xfefefe)>>1);

		mDirtyDiskBrush = CreateSolidBrush(c);
		mDirtyDiskColor = c;
	}

	for(int i=0; i<8; ++i) {
		uint32 id = kWriteModeID[i];
		CBAddString(id, L"Off");
		CBAddString(id, L"R/O");
		CBAddString(id, L"VirtRW");
		CBAddString(id, L"R/W");
	}

	return VDDialogFrameW32::OnLoaded();
}

void ATDiskDriveDialog::OnDestroy() {
	if (mDirtyDiskBrush) {
		DeleteObject(mDirtyDiskBrush);
		mDirtyDiskBrush = NULL;
	}
}

void ATDiskDriveDialog::OnDataExchange(bool write) {
	if (!write) {
		CheckButton(IDC_DRIVES1_8, !mbHighDrives);
		CheckButton(IDC_DRIVES9_15, mbHighDrives);

		ShowControl(IDC_STATIC_D8, !mbHighDrives);
		ShowControl(IDC_DISKPATH8, !mbHighDrives);
		ShowControl(IDC_WRITEMODE8, !mbHighDrives);
		ShowControl(IDC_BROWSE8, !mbHighDrives);
		ShowControl(IDC_EJECT8, !mbHighDrives);
		ShowControl(IDC_NEWDISK8, !mbHighDrives);
		ShowControl(IDC_SAVEAS8, !mbHighDrives);

		for(int i=0; i<8; ++i) {
			int driveIdx = i;

			if (mbHighDrives) {
				if (i == 7)
					break;

				driveIdx += 8;
			}

			if (driveIdx < 9)
				SetControlTextF(kDriveLabelID[i], L"D&%c:", '1' + driveIdx);
			else
				SetControlTextF(kDriveLabelID[i], L"D1&%c:", '0' + (driveIdx - 9));

			ATDiskEmulator& disk = g_sim.GetDiskDrive(driveIdx);
			SetControlText(kDiskPathID[i], disk.GetPath());

			CBSetSelectedIndex(kWriteModeID[i], !disk.IsEnabled() ? 0 : disk.IsWriteEnabled() ? disk.IsAutoFlushEnabled() ? 3 : 2 : 1);
		}
	}
}

bool ATDiskDriveDialog::OnCommand(uint32 id, uint32 extcode) {
	int index = 0;

	switch(id) {
		case IDC_EJECT8:	++index;
		case IDC_EJECT7:	++index;
		case IDC_EJECT6:	++index;
		case IDC_EJECT5:	++index;
		case IDC_EJECT4:	++index;
		case IDC_EJECT3:	++index;
		case IDC_EJECT2:	++index;
		case IDC_EJECT1:
			{
				int driveIndex = index;

				if (mbHighDrives)
					driveIndex += 8;

				ATDiskEmulator& disk = g_sim.GetDiskDrive(driveIndex);

				if (disk.IsDiskLoaded()) {
					disk.UnloadDisk();
					SetControlText(kDiskPathID[index], L"");
				} else {
					disk.SetEnabled(false);
					CBSetSelectedIndex(kWriteModeID[index], 0);
				}
			}
			return true;

		case IDC_BROWSE8:	++index;
		case IDC_BROWSE7:	++index;
		case IDC_BROWSE6:	++index;
		case IDC_BROWSE5:	++index;
		case IDC_BROWSE4:	++index;
		case IDC_BROWSE3:	++index;
		case IDC_BROWSE2:	++index;
		case IDC_BROWSE1:
			{
				int driveIndex = index;
				if (mbHighDrives)
					driveIndex += 8;

				VDStringW s(VDGetLoadFileName('disk', (VDGUIHandle)mhdlg, L"Load disk image",
					L"All supported types\0*.atr;*.pro;*.atx;*.xfd;*.zip\0"
					L"Atari disk image (*.atr, *.xfd)\0*.atr;*.xfd\0"
					L"Protected disk image (*.pro)\0*.pro\0"
					L"VAPI disk image (*.atx)\0*.atx\0"
					L"Zip archive (*.zip)\0*.zip\0"
					L"All files\0*.*\0"
					, L"atr"));

				if (!s.empty()) {
					ATDiskEmulator& disk = g_sim.GetDiskDrive(index);

					try {
						ATLoadContext ctx;
						ctx.mLoadType = kATLoadType_Disk;
						ctx.mLoadIndex = index;

						bool writeEnabled = disk.IsWriteEnabled();
						bool autoFlushEnabled = disk.IsAutoFlushEnabled();

						g_sim.Load(s.c_str(), writeEnabled, writeEnabled && autoFlushEnabled, &ctx);
						OnDataExchange(false);
					} catch(const MyError& e) {
						e.post(mhdlg, "Disk load error");
					}
				}
			}
			return true;

		case IDC_NEWDISK8:	++index;
		case IDC_NEWDISK7:	++index;
		case IDC_NEWDISK6:	++index;
		case IDC_NEWDISK5:	++index;
		case IDC_NEWDISK4:	++index;
		case IDC_NEWDISK3:	++index;
		case IDC_NEWDISK2:	++index;
		case IDC_NEWDISK1:
			{
				int driveIndex = index;
				if (mbHighDrives)
					driveIndex += 8;

				ATNewDiskDialog dlg;
				if (dlg.ShowDialog((VDGUIHandle)mhdlg)) {
					ATDiskEmulator& disk = g_sim.GetDiskDrive(driveIndex);

					disk.UnloadDisk();
					disk.CreateDisk(dlg.GetSectorCount(), dlg.GetBootSectorCount(), dlg.GetSectorSize());
					disk.SetWriteFlushMode(true, false);

					SetControlText(kDiskPathID[index], disk.GetPath());
					CBSetSelectedIndex(kWriteModeID[index], 2);
				}
			}
			return true;

		case IDC_SAVEAS8:	++index;
		case IDC_SAVEAS7:	++index;
		case IDC_SAVEAS6:	++index;
		case IDC_SAVEAS5:	++index;
		case IDC_SAVEAS4:	++index;
		case IDC_SAVEAS3:	++index;
		case IDC_SAVEAS2:	++index;
		case IDC_SAVEAS1:
			{
				int driveIndex = index;
				if (mbHighDrives)
					driveIndex += 8;

				ATDiskEmulator& disk = g_sim.GetDiskDrive(driveIndex);

				if (disk.GetPath()) {
					VDStringW s(VDGetSaveFileName(
							'disk',
							(VDGUIHandle)mhdlg,
							L"Save disk image",
							L"Atari disk image (*.atr)\0*.atr\0All files\0*.*\0",
							L"atr"));

					if (!s.empty()) {
						try {
							disk.SaveDisk(s.c_str());

							// if the disk is in VirtR/W mode, switch to R/W mode
							if (disk.IsWriteEnabled() && !disk.IsAutoFlushEnabled()) {
								disk.SetWriteFlushMode(true, true);
								OnDataExchange(false);
							}

							SetControlText(kDiskPathID[index], s.c_str());
						} catch(const MyError& e) {
							e.post(mhdlg, "Disk load error");
						}
					}
				}
			}
			return true;

		case IDC_WRITEMODE8:	++index;
		case IDC_WRITEMODE7:	++index;
		case IDC_WRITEMODE6:	++index;
		case IDC_WRITEMODE5:	++index;
		case IDC_WRITEMODE4:	++index;
		case IDC_WRITEMODE3:	++index;
		case IDC_WRITEMODE2:	++index;
		case IDC_WRITEMODE1:
			{
				int driveIndex = index;
				if (mbHighDrives)
					driveIndex += 8;

				int mode = CBGetSelectedIndex(id);
				ATDiskEmulator& disk = g_sim.GetDiskDrive(driveIndex);

				if (mode == 0) {
					disk.UnloadDisk();
					disk.SetEnabled(false);
				} else {
					disk.SetEnabled(true);
					disk.SetWriteFlushMode(mode > 1, mode == 3);
				}
			}
			return true;

		case IDC_DRIVES1_8:
			if (mbHighDrives && IsButtonChecked(id)) {
				mbHighDrives = false;

				OnDataExchange(false);
			}
			return true;

		case IDC_DRIVES9_15:
			if (!mbHighDrives && IsButtonChecked(id)) {
				mbHighDrives = true;

				OnDataExchange(false);
			}
			return true;
	}

	return false;
}

VDZINT_PTR ATDiskDriveDialog::DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	int index;

	switch(msg) {
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLOREDIT:
			index = 0;

			switch(GetWindowLong((HWND)lParam, GWL_ID)) {
				case IDC_DISKPATH8:	++index;
				case IDC_DISKPATH7:	++index;
				case IDC_DISKPATH6:	++index;
				case IDC_DISKPATH5:	++index;
				case IDC_DISKPATH4:	++index;
				case IDC_DISKPATH3:	++index;
				case IDC_DISKPATH2:	++index;
				case IDC_DISKPATH1:
					{
						int driveIndex = index;
						if (mbHighDrives)
							driveIndex += 8;

						ATDiskEmulator& disk = g_sim.GetDiskDrive(driveIndex);
						if (disk.IsDirty()) {
							HDC hdc = (HDC)wParam;

							SetBkColor(hdc, mDirtyDiskColor);
							return (VDZINT_PTR)mDirtyDiskBrush;
						}
					}
					break;
			}

	}

	return VDDialogFrameW32::DlgProc(msg, wParam, lParam);
}

void ATUIShowDiskDriveDialog(VDGUIHandle hParent) {
	ATDiskDriveDialog().ShowDialog(hParent);
}
