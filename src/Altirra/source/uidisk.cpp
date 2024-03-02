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
#include <vd2/system/w32assist.h>
#include <vd2/Dita/services.h>
#include <at/atui/dialog.h>
#include "resource.h"
#include "disk.h"
#include "diskfs.h"
#include "simulator.h"

extern ATSimulator g_sim;

void ATUIShowDialogDiskExplorer(VDGUIHandle h, IATDiskImage *image, const wchar_t *imageName, bool writeEnabled, bool autoFlush);

enum ATDiskFormatFileSystem {
	kATDiskFFS_None,
	kATDiskFFS_DOS2
};

class ATNewDiskDialog : public VDDialogFrameW32 {
public:
	ATNewDiskDialog();
	~ATNewDiskDialog();

	uint32 GetSectorCount() const { return mSectorCount; }
	uint32 GetBootSectorCount() const { return mBootSectorCount; }
	uint32 GetSectorSize() const { return mSectorSize; }
	ATDiskFormatFileSystem GetFormatFFS() const { return mDiskFFS; }

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
	ATDiskFormatFileSystem mDiskFFS;
	uint32	mSectorCount;
	uint32	mBootSectorCount;
	uint32	mSectorSize;
};

ATNewDiskDialog::ATNewDiskDialog()
	: VDDialogFrameW32(IDD_CREATE_DISK)
	, mFormat(kFormatSingle)
	, mDiskFFS(kATDiskFFS_None)
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

	CBAddString(IDC_FILESYSTEM, L"None (unformatted)");
	CBAddString(IDC_FILESYSTEM, L"DOS 2");

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

		switch(CBGetSelectedIndex(IDC_FILESYSTEM)) {
			case 0:
			default:
				mDiskFFS = kATDiskFFS_None;
				break;

			case 1:
				if (mSectorSize != 128 || mSectorCount != 720 || mBootSectorCount != 3) {
					ShowError(L"The specified disk geometry is not supported for the selected filesystem.", L"Altirra Error");
					FailValidation(IDC_FILESYSTEM);
					return;
				}

				mDiskFFS = kATDiskFFS_DOS2;
				break;
		}

	} else {
		CheckButton(IDC_SECTOR_SIZE_128, mSectorSize == 128);
		CheckButton(IDC_SECTOR_SIZE_256, mSectorSize == 256);
		CheckButton(IDC_SECTOR_SIZE_512, mSectorSize == 512);
		CBSetSelectedIndex(IDC_FORMAT, (int)mFormat);
		UpdateEnables();

		CBSetSelectedIndex(IDC_FILESYSTEM, mDiskFFS);
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
	HBRUSH mVirtualDiskBrush;
	HBRUSH mVirtualFolderBrush;
	HICON mhEjectIcon;
	HFONT mhFontMarlett;
	COLORREF mDirtyDiskColor;
	COLORREF mVirtualDiskColor;
	COLORREF mVirtualFolderColor;
};

ATDiskDriveDialog::ATDiskDriveDialog()
	: VDDialogFrameW32(IDD_DISK_DRIVES)
	, mbHighDrives(false)
	, mDirtyDiskBrush(NULL)
	, mVirtualDiskBrush(NULL)
	, mVirtualFolderBrush(NULL)
	, mhEjectIcon(NULL)
	, mhFontMarlett(NULL)
{
}

ATDiskDriveDialog::~ATDiskDriveDialog() {
	if (mhEjectIcon)
		DeleteObject(mhEjectIcon);

	if (mhFontMarlett)
		DeleteObject(mhFontMarlett);
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

	const uint32 kEjectID[]={
		IDC_EJECT1,
		IDC_EJECT2,
		IDC_EJECT3,
		IDC_EJECT4,
		IDC_EJECT5,
		IDC_EJECT6,
		IDC_EJECT7,
		IDC_EJECT8,
	};

	const uint32 kMoreIds[]={
		IDC_MORE1,
		IDC_MORE2,
		IDC_MORE3,
		IDC_MORE4,
		IDC_MORE5,
		IDC_MORE6,
		IDC_MORE7,
		IDC_MORE8,
	};
}

bool ATDiskDriveDialog::OnLoaded() {
	if (!mhEjectIcon) {
		mhEjectIcon = (HICON)LoadImage(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDI_EJECT), IMAGE_ICON, 16, 16, 0);
	}

	if (mhEjectIcon) {
		for(size_t i=0; i<vdcountof(kEjectID); ++i) {
			HWND hwndControl = GetControl(kEjectID[i]);

			if (hwndControl)
				SendMessage(hwndControl, BM_SETIMAGE, IMAGE_ICON, (LPARAM)mhEjectIcon);
		}
	}

	if (!mhFontMarlett) {
		HFONT hfontDlg = (HFONT)SendMessage(mhdlg, WM_GETFONT, 0, 0);

		if (hfontDlg) {
			LOGFONT lf = {0};
			if (GetObject(hfontDlg, sizeof lf, &lf)) {
				mhFontMarlett = CreateFont(lf.lfHeight, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Marlett"));
			}
		}
	}

	if (mhFontMarlett) {
		for(size_t i=0; i<vdcountof(kMoreIds); ++i) {
			HWND hwndControl = GetControl(kMoreIds[i]);

			if (hwndControl) {
				SendMessage(hwndControl, WM_SETFONT, (WPARAM)mhFontMarlett, MAKELONG(TRUE, 0));
				SetWindowText(hwndControl, _T("4"));
			}
		}
	}

	if (!mDirtyDiskBrush) {
		DWORD c = GetSysColor(COLOR_3DFACE);

		// redden the color
		uint32 d = RGB(255, 128, 64);

		c = (c|d) - (((c^d) & 0xfefefe)>>1);

		mDirtyDiskBrush = CreateSolidBrush(c);
		mDirtyDiskColor = c;
	}

	if (!mVirtualDiskBrush) {
		DWORD c = GetSysColor(COLOR_3DFACE);

		// bluify the color
		uint32 d = RGB(64, 128, 255);

		c = (c|d) - (((c^d) & 0xfefefe)>>1);

		mVirtualDiskBrush = CreateSolidBrush(c);
		mVirtualDiskColor = c;
	}

	if (!mVirtualFolderBrush) {
		DWORD c = GetSysColor(COLOR_3DFACE);

		// yellowify the color
		uint32 d = RGB(255, 224, 128);

		c = (c|d) - (((c^d) & 0xfefefe)>>1);

		mVirtualFolderBrush = CreateSolidBrush(c);
		mVirtualFolderColor = c;
	}

	for(int i=0; i<8; ++i) {
		uint32 id = kWriteModeID[i];
		CBAddString(id, L"Off");
		CBAddString(id, L"R/O");
		CBAddString(id, L"VirtRW");
		CBAddString(id, L"R/W");
	}

	CBAddString(IDC_EMULATION_LEVEL, L"Generic emulation (288 RPM)");
	CBAddString(IDC_EMULATION_LEVEL, L"Fastest possible (288 RPM, 128Kbps high speed)");
	CBAddString(IDC_EMULATION_LEVEL, L"810 (288 RPM)");
	CBAddString(IDC_EMULATION_LEVEL, L"1050 (288 RPM)");
	CBAddString(IDC_EMULATION_LEVEL, L"XF551 (300 RPM, 39Kbps high speed)");
	CBAddString(IDC_EMULATION_LEVEL, L"US-Doubler (288 RPM, 52Kbps high speed)");
	CBAddString(IDC_EMULATION_LEVEL, L"Speedy 1050 (288 RPM, 56Kbps high speed)");
	CBAddString(IDC_EMULATION_LEVEL, L"Indus GT (288 RPM, 68Kbps high speed)");
	CBAddString(IDC_EMULATION_LEVEL, L"Happy (288 RPM, 52Kbps high speed)");
	CBAddString(IDC_EMULATION_LEVEL, L"1050 Turbo (288 RPM, 68Kbps high speed)");

	CBSetSelectedIndex(IDC_EMULATION_LEVEL, (sint32)g_sim.GetDiskDrive(0).GetEmulationMode());

	return VDDialogFrameW32::OnLoaded();
}

void ATDiskDriveDialog::OnDestroy() {
	if (mDirtyDiskBrush) {
		DeleteObject(mDirtyDiskBrush);
		mDirtyDiskBrush = NULL;
	}

	if (mVirtualDiskBrush) {
		DeleteObject(mVirtualDiskBrush);
		mVirtualDiskBrush = NULL;
	}

	if (mVirtualFolderBrush) {
		DeleteObject(mVirtualFolderBrush);
		mVirtualFolderBrush = NULL;
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
		ShowControl(IDC_MORE8, !mbHighDrives);

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
	} else {
		ATDiskEmulationMode mode = (ATDiskEmulationMode)CBGetSelectedIndex(IDC_EMULATION_LEVEL);
		for(int i=0; i<15; ++i)
			g_sim.GetDiskDrive(i).SetEmulationMode(mode);
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
					L"All supported types\0*.atr;*.pro;*.atx;*.xfd;*.dcm;*.zip\0"
					L"Atari disk image (*.atr, *.xfd)\0*.atr;*.xfd;*.dcm\0"
					L"Protected disk image (*.pro)\0*.pro\0"
					L"VAPI disk image (*.atx)\0*.atx\0"
					L"Zip archive (*.zip)\0*.zip\0"
					L"Gzip archive (*.gz;*.atz)\0*.gz;*.atz\0"
					L"All files\0*.*\0"
					, L"atr"));

				if (!s.empty()) {
					ATDiskEmulator& disk = g_sim.GetDiskDrive(index);

					try {
						ATLoadContext ctx;
						ctx.mLoadType = kATLoadType_Disk;
						ctx.mLoadIndex = driveIndex;

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

		case IDC_MORE8:	++index;
		case IDC_MORE7:	++index;
		case IDC_MORE6:	++index;
		case IDC_MORE5:	++index;
		case IDC_MORE4:	++index;
		case IDC_MORE3:	++index;
		case IDC_MORE2:	++index;
		case IDC_MORE1:
			{
				int driveIndex = index;
				if (mbHighDrives)
					driveIndex += 8;

				UINT selectedId = 0;

				HMENU hmenu = LoadMenu(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDR_DISK_CONTEXT_MENU));
				if (hmenu) {
					HMENU hsubmenu = GetSubMenu(hmenu, 0);

					if (hsubmenu) {
						RECT r = {0};
						if (HWND hwndItem = GetDlgItem(mhdlg, id))
							GetWindowRect(hwndItem, &r);

						TPMPARAMS params = {sizeof(TPMPARAMS)};
						params.rcExclude = r;
						selectedId = (UINT)TrackPopupMenuEx(hsubmenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_HORIZONTAL | TPM_NONOTIFY | TPM_RETURNCMD, r.right, r.top, mhdlg, &params);
					}

					DestroyMenu(hmenu);
				}

				ATDiskEmulator& disk = g_sim.GetDiskDrive(driveIndex);

				switch(selectedId) {
					case ID_CONTEXT_NEWDISK:
						{
							ATNewDiskDialog dlg;
							if (dlg.ShowDialog((VDGUIHandle)mhdlg)) {

								disk.UnloadDisk();
								disk.CreateDisk(dlg.GetSectorCount(), dlg.GetBootSectorCount(), dlg.GetSectorSize());
								disk.SetWriteFlushMode(true, false);

								switch(dlg.GetFormatFFS()) {
									case kATDiskFFS_DOS2:
										try {
											vdautoptr<IATDiskFS> fs(ATDiskFormatImageDOS2(disk.GetDiskImage()));

											fs->Flush();
										} catch(const MyError& e) {
											e.post(mhdlg, "Format error");
										}
										break;

								}

								SetControlText(kDiskPathID[index], disk.GetPath());
								CBSetSelectedIndex(kWriteModeID[index], 2);
							}
						}
						break;

					case ID_CONTEXT_EXPLOREDISK:
						if (IATDiskImage *image = disk.GetDiskImage()) {
							VDStringW imageName;

							imageName.sprintf(L"Mounted disk on D%u:", driveIndex + 1);

							ATUIShowDialogDiskExplorer((VDGUIHandle)mhdlg, image, imageName.c_str(), disk.IsWriteEnabled(), disk.IsAutoFlushEnabled());

							// invalidate the path widget in case the disk has been dirtied
							HWND hwndPathControl = GetControl(kDiskPathID[index]);

							if (hwndPathControl)
								InvalidateRect(hwndPathControl, NULL, TRUE);
						}
						break;

					case ID_CONTEXT_MOUNTFOLDERDOS2:
					case ID_CONTEXT_MOUNTFOLDERSDFS:
						{
							const VDStringW& path = VDGetDirectory('vfol', (VDGUIHandle)mhdlg, L"Select folder for virtual disk image");

							if (!path.empty()) {
								try {
									disk.MountFolder(path.c_str(), selectedId == ID_CONTEXT_MOUNTFOLDERSDFS);

									OnDataExchange(false);
								} catch(const MyError& e) {
									e.post(mhdlg, "Mount error");
								}
							}
						}
						break;

					case ID_CONTEXT_SAVEDISK:
						if (disk.IsDiskLoaded() && !disk.GetDiskImage()->IsDynamic()) {
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
						break;
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
						} else if (disk.IsDiskLoaded()) {
							if (wcschr(disk.GetPath(), L'*')) {
								HDC hdc = (HDC)wParam;

								SetBkColor(hdc, mVirtualFolderColor);
								return (VDZINT_PTR)mVirtualFolderBrush;
							} else if (!disk.IsDiskBacked()) {
								HDC hdc = (HDC)wParam;

								SetBkColor(hdc, mVirtualDiskColor);
								return (VDZINT_PTR)mVirtualDiskBrush;
							}
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
