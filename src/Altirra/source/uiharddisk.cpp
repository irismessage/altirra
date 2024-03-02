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
#include <vd2/system/math.h>
#include <vd2/system/strutil.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/services.h>
#include <at/atui/dialog.h>
#include "resource.h"
#include "ide.h"
#include "idephysdisk.h"
#include "idevhdimage.h"
#include "simulator.h"
#include "oshelper.h"
#include "uiprogress.h"
#include <at/atui/uiproxies.h>

#ifndef BCM_SETSHIELD
#define BCM_SETSHIELD	0x160C
#endif

extern ATSimulator g_sim;

VDStringW ATUIShowDialogBrowsePhysicalDisks(VDGUIHandle hParent);

///////////////////////////////////////////////////////////////////////////

class ATUIDialogCreateVHDImage : public VDDialogFrameW32 {
public:
	ATUIDialogCreateVHDImage();
	~ATUIDialogCreateVHDImage();

	uint32 GetSectorCount() const { return mSectorCount; }
	const wchar_t *GetPath() const { return mPath.c_str(); }

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	bool OnOK();
	bool OnCommand(uint32 id, uint32 extcode);
	void UpdateGeometry();
	void UpdateEnables();

	VDStringW mPath;
	uint32 mSectorCount;
	uint32 mSizeInMB;
	uint32 mHeads;
	uint32 mSPT;
	bool mbAutoGeometry;
	bool mbDynamicDisk;
	uint32 mInhibitUpdateLocks;
};

ATUIDialogCreateVHDImage::ATUIDialogCreateVHDImage()
	: VDDialogFrameW32(IDD_CREATE_VHD)
	, mSectorCount(8*1024*2)		// 8MB
	, mSizeInMB(8)
	, mHeads(15)
	, mSPT(63)
	, mbAutoGeometry(true)
	, mbDynamicDisk(true)
	, mInhibitUpdateLocks(0)
{
}

ATUIDialogCreateVHDImage::~ATUIDialogCreateVHDImage() {
}

bool ATUIDialogCreateVHDImage::OnLoaded() {
	UpdateGeometry();
	UpdateEnables();

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogCreateVHDImage::OnDataExchange(bool write) {
	ExchangeControlValueString(write, IDC_PATH, mPath);
	ExchangeControlValueUint32(write, IDC_SIZE_SECTORS, mSectorCount, 2048, 0xFFFFFFFEU);
	ExchangeControlValueUint32(write, IDC_SIZE_MB, mSizeInMB, 1, 4095);

	if (write) {
		mbAutoGeometry = IsButtonChecked(IDC_GEOMETRY_AUTO);
		mbDynamicDisk = IsButtonChecked(IDC_TYPE_DYNAMIC);
	} else {
		CheckButton(IDC_GEOMETRY_AUTO, mbAutoGeometry);
		CheckButton(IDC_GEOMETRY_MANUAL, !mbAutoGeometry);

		CheckButton(IDC_TYPE_FIXED, !mbDynamicDisk);
		CheckButton(IDC_TYPE_DYNAMIC, mbDynamicDisk);
	}

	if (!write || mbAutoGeometry) {
		ExchangeControlValueUint32(write, IDC_HEADS, mHeads, 1, 16);
		ExchangeControlValueUint32(write, IDC_SPT, mHeads, 1, 255);
	}
}

bool ATUIDialogCreateVHDImage::OnOK() {
	if (VDDialogFrameW32::OnOK())
		return true;

	// Okay, let's actually try to create the VHD image!
	VDZHWND prevProgressParent = ATUISetProgressWindowParentW32(mhdlg);

	try {
		ATIDEVHDImage vhd;
		vhd.InitNew(mPath.c_str(), mHeads, mSPT, mSectorCount, mbDynamicDisk);
		vhd.Flush();
	} catch(const MyUserAbortError&) {
		ATUISetProgressWindowParentW32(prevProgressParent);
		return true;
	} catch(const MyError& e) {
		VDStringW msg;
		msg.sprintf(L"VHD creation failed: %hs", e.gets());
		ShowError(msg.c_str(), L"Altirra Error");
		ATUISetProgressWindowParentW32(prevProgressParent);
		return true;
	}

	ATUISetProgressWindowParentW32(prevProgressParent);

	ShowInfo(L"VHD creation was successful.", L"Altirra Notice");
	return false;
}

bool ATUIDialogCreateVHDImage::OnCommand(uint32 id, uint32 extcode) {
	int index = 0;

	switch(id) {
		case IDC_BROWSE:
			{
				VDStringW s(VDGetSaveFileName('vhd ', (VDGUIHandle)mhdlg, L"Select location for new VHD image file", L"Virtual hard disk image\0*.vhd\0", L"vhd"));
				if (!s.empty())
					SetControlText(IDC_PATH, s.c_str());
			}
			return true;

		case IDC_GEOMETRY_AUTO:
		case IDC_GEOMETRY_MANUAL:
			if (extcode == BN_CLICKED)
				UpdateEnables();
			return true;

		case IDC_SIZE_MB:
			if (extcode == EN_UPDATE && !mInhibitUpdateLocks) {
				uint32 mb = GetControlValueUint32(IDC_SIZE_MB);

				if (mb) {
					++mInhibitUpdateLocks;
					SetControlTextF(IDC_SIZE_SECTORS, L"%u", mb * 2048);
					--mInhibitUpdateLocks;
				}
			}
			return true;

		case IDC_SIZE_SECTORS:
			if (extcode == EN_UPDATE && !mInhibitUpdateLocks) {
				uint32 sectors = GetControlValueUint32(IDC_SIZE_SECTORS);

				if (sectors) {
					++mInhibitUpdateLocks;
					SetControlTextF(IDC_SIZE_MB, L"%u", sectors >> 11);
					--mInhibitUpdateLocks;
				}
			}
			return true;
	}

	return false;
}

void ATUIDialogCreateVHDImage::UpdateGeometry() {
	// This calculation is from the VHD spec.
	uint32 secCount = std::min<uint32>(mSectorCount, 65535*16*255);

	if (secCount >= 65535*16*63) {
		mSPT = 255;
		mHeads = 16;
	} else {
		mSPT = 17;

		uint32 tracks = secCount / 17;
		uint32 heads = (tracks + 1023) >> 10;

		if (heads < 4) {
			heads = 4;
		}
		
		if (tracks >= (heads * 1024) || heads > 16) {
			mSPT = 31;
			heads = 16;
			tracks = secCount / 31;
		}

		if (tracks >= (heads * 1024)) {
			mSPT = 63;
			heads = 16;
		}

		mHeads = heads;
	}

	SetControlTextF(IDC_HEADS, L"%u", mHeads);
	SetControlTextF(IDC_SPT, L"%u", mSPT);
}

void ATUIDialogCreateVHDImage::UpdateEnables() {
	bool enableManualControls = IsButtonChecked(IDC_GEOMETRY_MANUAL);

	EnableControl(IDC_STATIC_HEADS, enableManualControls);
	EnableControl(IDC_STATIC_SPT, enableManualControls);
	EnableControl(IDC_HEADS, enableManualControls);
	EnableControl(IDC_SPT, enableManualControls);
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogHardDisk : public VDDialogFrameW32 {
public:
	ATUIDialogHardDisk();
	~ATUIDialogHardDisk();

	bool OnLoaded();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void Update(int id);

protected:
	void UpdateEnables();
	void UpdateGeometry();
	void UpdateCapacity();
	void SetCapacityBySectorCount(uint64 sectors);

	uint32 mInhibitUpdateLocks;

	VDUIProxyComboBoxControl mComboHWMode;
};

ATUIDialogHardDisk::ATUIDialogHardDisk()
	: VDDialogFrameW32(IDD_HARD_DISK)
	, mInhibitUpdateLocks(0)
{
}

ATUIDialogHardDisk::~ATUIDialogHardDisk() {
}

bool ATUIDialogHardDisk::OnLoaded() {
	if (VDIsAtLeastVistaW32()) {
		HWND hwndItem = GetDlgItem(mhdlg, IDC_IDE_DISKBROWSE);

		if (hwndItem)
			SendMessage(hwndItem, BCM_SETSHIELD, 0, TRUE);
	}

	AddProxy(&mComboHWMode, IDC_HW_MODE);

	ATUIEnableEditControlAutoComplete(GetControl(IDC_IDE_IMAGEPATH));

	mComboHWMode.AddItem(L"MyIDE internal ($D1xx)");
	mComboHWMode.AddItem(L"MyIDE external ($D5xx)");
	mComboHWMode.AddItem(L"MyIDE II");
	mComboHWMode.AddItem(L"KMK/JZ IDE V1");
	mComboHWMode.AddItem(L"KMK/JZ IDE V2 (IDEPlus)");
	mComboHWMode.AddItem(L"SIDE");
	mComboHWMode.AddItem(L"SIDE 2");

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogHardDisk::OnDataExchange(bool write) {
	if (!write) {
		bool ideHardware = (g_sim.GetSIDE() || g_sim.GetKMKJZIDE() || g_sim.GetMyIDE());

		CheckButton(IDC_IDE_ENABLE, ideHardware);

		if (ideHardware) {
			ATIDEEmulator *ide = g_sim.GetIDEEmulator();

			if (ide) {
				SetControlText(IDC_IDE_IMAGEPATH, ide->GetImagePath());
				CheckButton(IDC_IDEREADONLY, !ide->IsWriteEnabled());

				uint32 cylinders = ide->GetCylinderCount();
				uint32 heads = ide->GetHeadCount();
				uint32 spt = ide->GetSectorsPerTrack();

				if (!cylinders || !heads || !spt) {
					heads = 16;
					spt = 63;
					cylinders = 20;
				} else {
					SetControlTextF(IDC_IDE_CYLINDERS, L"%u", cylinders);
					SetControlTextF(IDC_IDE_HEADS, L"%u", heads);
					SetControlTextF(IDC_IDE_SPT, L"%u", spt);
				}

				bool fast = ide->IsFastDevice();
				CheckButton(IDC_SPEED_FAST, fast);
				CheckButton(IDC_SPEED_SLOW, !fast);

				UpdateCapacity();
			} else {
				CheckButton(IDC_SPEED_FAST, true);
				SetControlText(IDC_IDE_IMAGEPATH, L"");
			}

			ATIDEHardwareMode hwmode = g_sim.GetIDEHardwareMode();
			int hwidx = 0;
			switch(hwmode) {
				case kATIDEHardwareMode_MyIDE_D1xx: hwidx = 0; break;
				case kATIDEHardwareMode_MyIDE_D5xx: hwidx = 1; break;
				case kATIDEHardwareMode_MyIDE_V2_D5xx: hwidx = 2; break;
				case kATIDEHardwareMode_KMKJZ_V1: hwidx = 3; break;
				case kATIDEHardwareMode_KMKJZ_V2: hwidx = 4; break;
				case kATIDEHardwareMode_SIDE: hwidx = 5; break;
				case kATIDEHardwareMode_SIDE2: hwidx = 6; break;
			}

			mComboHWMode.SetSelection(hwidx);
		} else {
			mComboHWMode.SetSelection(1);		// MyIDE D5xx
			CheckButton(IDC_SPEED_FAST, true);
		}

		UpdateEnables();
	} else {
		bool enable = IsButtonChecked(IDC_ENABLE);
		bool reset = false;

		ATIDEEmulator *ide = g_sim.GetIDEEmulator();
		ATIDEHardwareMode hwmode = kATIDEHardwareMode_None;
		if (IsButtonChecked(IDC_IDE_ENABLE)) {
			switch(mComboHWMode.GetSelection()) {
				case 0: hwmode = kATIDEHardwareMode_MyIDE_D1xx; break;
				case 1: hwmode = kATIDEHardwareMode_MyIDE_D5xx; break;
				case 2: hwmode = kATIDEHardwareMode_MyIDE_V2_D5xx; break;
				case 3: hwmode = kATIDEHardwareMode_KMKJZ_V1; break;
				case 4: hwmode = kATIDEHardwareMode_KMKJZ_V2; break;
				case 5: hwmode = kATIDEHardwareMode_SIDE; break;
				case 6: hwmode = kATIDEHardwareMode_SIDE2; break;
			}

			const bool write = !IsButtonChecked(IDC_IDEREADONLY);
			const bool fast = IsButtonChecked(IDC_SPEED_FAST);

			VDStringW path;
			GetControlText(IDC_IDE_IMAGEPATH, path);

			uint32 cylinders = 0;
			uint32 heads = 0;
			uint32 sectors = 0;

			if (!path.empty()) {
				ExchangeControlValueUint32(true, IDC_IDE_CYLINDERS, cylinders, 1, 16777216);
				ExchangeControlValueUint32(true, IDC_IDE_HEADS, heads, 1, 16);
				ExchangeControlValueUint32(true, IDC_IDE_SPT, sectors, 1, 255);
			}

			if (!mbValidationFailed) {
				bool hwchanged = false;
				if (g_sim.GetIDEHardwareMode() != hwmode)
					hwchanged = true;

				bool changed = true;

				if (ide) {
					if (!path.empty()) {
						changed = false;

						if (ide->GetImagePath() != path)
							changed = true;
						else if (ide->IsWriteEnabled() != write)
							changed = true;
						else if (ide->GetCylinderCount() != cylinders)
							changed = true;
						else if (ide->GetHeadCount() != heads)
							changed = true;
						else if (ide->GetSectorsPerTrack() != sectors)
							changed = true;
						else if (ide->IsFastDevice() != fast)
							changed = true;
					}
				} else {
					if (path.empty())
						changed = false;
				}

				try {
					if (hwchanged) {
						g_sim.LoadIDE(hwmode);
						reset = true;
					}

					if (changed) {
						if (path.empty())
							g_sim.UnloadIDEImage();
						else
							g_sim.LoadIDEImage(write, fast, cylinders, heads, sectors, path.c_str());
					}
				} catch(const MyError& e) {
					e.post(mhdlg, "Altirra Error");
					FailValidation(IDC_IDE_IMAGEPATH);
					// fall through to force a reset
				}
			}
		} else {
			if (g_sim.GetKMKJZIDE() || g_sim.GetSIDE() || g_sim.GetMyIDE()) {
				g_sim.UnloadIDE();
				reset = true;
			}

			if (ide)
				g_sim.UnloadIDEImage();
		}

		if (reset)
			g_sim.ColdReset();
	}
}

bool ATUIDialogHardDisk::OnCommand(uint32 id, uint32 extcode) {
	int index = 0;

	switch(id) {
		case IDC_IDE_IMAGEBROWSE:
			{
				int optvals[1]={false};

				static const VDFileDialogOption kOpts[]={
					{ VDFileDialogOption::kConfirmFile, 0 },
					{0}
				};

				VDStringW s(VDGetSaveFileName('ide ', (VDGUIHandle)mhdlg, L"Select IDE image file", L"All files\0*.*\0", NULL, kOpts, optvals));
				if (!s.empty()) {
					if (s.size() >= 4 && !vdwcsicmp(s.c_str() + s.size() - 4, L".vhd")) {
						try {
							vdrefptr<ATIDEVHDImage> vhdImage(new ATIDEVHDImage);

							vhdImage->Init(s.c_str(), false);

							SetCapacityBySectorCount(vhdImage->GetSectorCount());
						} catch(const MyError& e) {
							e.post(mhdlg, "Altirra Error");
							return true;
						}
					}

					SetControlText(IDC_IDE_IMAGEPATH, s.c_str());
				}
			}
			return true;

		case IDC_EJECT:
			SetControlText(IDC_IDE_IMAGEPATH, L"");
			return true;

		case IDC_IDE_DISKBROWSE:
			if (!ATIsUserAdministrator()) {
				ShowError(L"You must run Altirra with local administrator access in order to mount a physical disk for emulation.", L"Altirra Error");
				return true;
			} else {
				ShowWarning(
					L"This option uses a physical disk for IDE emulation. You can either map the entire disk or a partition within the disk. However, only read only access is supported.\n"
					L"\n"
					L"You can use a partition that is currently mounted by Windows. However, changes to the file system in Windows may not be reflected consistently in the emulator.",
					L"Altirra Warning");
			}

			{
				const VDStringW& path = ATUIShowDialogBrowsePhysicalDisks((VDGUIHandle)mhdlg);

				if (!path.empty()) {
					SetControlText(IDC_IDE_IMAGEPATH, path.c_str());
					CheckButton(IDC_READONLY, true);

					sint64 size = ATIDEGetPhysicalDiskSize(path.c_str());
					uint64 sectors = (uint64)size >> 9;

					SetCapacityBySectorCount(sectors);
				}
			}
			return true;

		case IDC_CREATE_VHD:
			{
				ATUIDialogCreateVHDImage createVHDDlg;

				if (createVHDDlg.ShowDialog((VDGUIHandle)mhdlg)) {
					SetCapacityBySectorCount(createVHDDlg.GetSectorCount());
					SetControlText(IDC_IDE_IMAGEPATH, createVHDDlg.GetPath());
				}
			}
			return true;

		case IDC_IDE_ENABLE:
			if (extcode == BN_CLICKED)
				UpdateEnables();
			return true;

		case IDC_IDE_CYLINDERS:
		case IDC_IDE_HEADS:
		case IDC_IDE_SPT:
			if (extcode == EN_UPDATE && !mInhibitUpdateLocks)
				UpdateCapacity();
			return true;

		case IDC_IDE_SIZE:
			if (extcode == EN_UPDATE && !mInhibitUpdateLocks)
				UpdateGeometry();
			return true;
	}

	return false;
}

void ATUIDialogHardDisk::UpdateEnables() {
	bool ideenable = IsButtonChecked(IDC_IDE_ENABLE);

	EnableControl(IDC_STATIC_IDE_IMAGEPATH, ideenable);
	EnableControl(IDC_IDE_IMAGEPATH, ideenable);
	EnableControl(IDC_IDE_IMAGEBROWSE, ideenable);
	EnableControl(IDC_IDE_DISKBROWSE, ideenable);
	EnableControl(IDC_CREATE_VHD, ideenable);
	EnableControl(IDC_IDEREADONLY, ideenable);
	EnableControl(IDC_EJECT, ideenable);
	EnableControl(IDC_STATIC_IDE_GEOMETRY, ideenable);
	EnableControl(IDC_STATIC_IDE_CYLINDERS, ideenable);
	EnableControl(IDC_STATIC_IDE_HEADS, ideenable);
	EnableControl(IDC_STATIC_IDE_SPT, ideenable);
	EnableControl(IDC_STATIC_IDE_SIZE, ideenable);
	EnableControl(IDC_IDE_CYLINDERS, ideenable);
	EnableControl(IDC_IDE_HEADS, ideenable);
	EnableControl(IDC_IDE_SPT, ideenable);
	EnableControl(IDC_IDE_SIZE, ideenable);
	EnableControl(IDC_STATIC_IDE_IOREGION, ideenable);
	EnableControl(IDC_HW_MODE, ideenable);
	EnableControl(IDC_STATIC_IDE_SPEED, ideenable);
	EnableControl(IDC_SPEED_SLOW, ideenable);
	EnableControl(IDC_SPEED_FAST, ideenable);
}

void ATUIDialogHardDisk::UpdateGeometry() {
	uint32 imageSizeMB = GetControlValueUint32(IDC_IDE_SIZE);

	if (imageSizeMB) {
		uint32 heads;
		uint32 sectors;
		uint32 cylinders;

		if (imageSizeMB <= 64) {
			heads = 4;
			sectors = 32;
			cylinders = imageSizeMB << 4;
		} else {
			heads = 16;
			sectors = 63;
			cylinders = (imageSizeMB * 128 + 31) / 63;
		}

		if (cylinders > 16777216)
			cylinders = 16777216;

		++mInhibitUpdateLocks;
		SetControlTextF(IDC_IDE_CYLINDERS, L"%u", cylinders);
		SetControlTextF(IDC_IDE_HEADS, L"%u", heads);
		SetControlTextF(IDC_IDE_SPT, L"%u", sectors);
		--mInhibitUpdateLocks;
	}
}

void ATUIDialogHardDisk::UpdateCapacity() {
	uint32 cyls = GetControlValueUint32(IDC_IDE_CYLINDERS);
	uint32 heads = GetControlValueUint32(IDC_IDE_HEADS);
	uint32 spt = GetControlValueUint32(IDC_IDE_SPT);
	uint32 size = 0;

	if (cyls || heads || spt)
		size = (cyls * heads * spt) >> 11;

	++mInhibitUpdateLocks;

	if (size)
		SetControlTextF(IDC_IDE_SIZE, L"%u", size);
	else
		SetControlText(IDC_IDE_SIZE, L"--");

	--mInhibitUpdateLocks;
}

void ATUIDialogHardDisk::SetCapacityBySectorCount(uint64 sectors) {
	uint32 spt = 63;
	uint32 heads = 15;
	uint32 cylinders = 1;

	if (sectors)
		cylinders = VDClampToUint32((sectors - 1) / (heads * spt) + 1);

	SetControlTextF(IDC_IDE_CYLINDERS, L"%u", cylinders);
	SetControlTextF(IDC_IDE_HEADS, L"%u", heads);
	SetControlTextF(IDC_IDE_SPT, L"%u", spt);

	UpdateCapacity();
}

void ATUIShowHardDiskDialog(VDGUIHandle hParent) {
	ATUIDialogHardDisk dlg;

	dlg.ShowDialog(hParent);
}
