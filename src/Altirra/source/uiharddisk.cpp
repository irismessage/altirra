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
#include "ide.h"
#include "simulator.h"
#include "oshelper.h"

#ifndef BCM_SETSHIELD
#define BCM_SETSHIELD	0x160C
#endif

extern ATSimulator g_sim;

VDStringW ATUIShowDialogBrowsePhysicalDisks(VDGUIHandle hParent);

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
	void UpdateCapacity();
	void UpdateGeometry();

	uint32 mInhibitUpdateLocks;
};

ATUIDialogHardDisk::ATUIDialogHardDisk()
	: VDDialogFrameW32(IDD_HARD_DISK)
	, mInhibitUpdateLocks(0)
{
}

ATUIDialogHardDisk::~ATUIDialogHardDisk() {
}

bool ATUIDialogHardDisk::OnLoaded() {
	if (LOBYTE(LOWORD(GetVersion())) >= 6) {
		HWND hwndItem = GetDlgItem(mhdlg, IDC_IDE_DISKBROWSE);

		if (hwndItem)
			SendMessage(hwndItem, BCM_SETSHIELD, 0, TRUE);
	}

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogHardDisk::OnDataExchange(bool write) {
	if (!write) {
		ATIDEEmulator *ide = g_sim.GetIDEEmulator();
		CheckButton(IDC_IDE_ENABLE, ide != NULL);

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

			UpdateCapacity();

			ATIDEHardwareMode hwmode = g_sim.GetIDEHardwareMode();
			CheckButton(IDC_IDE_D1XX, hwmode == kATIDEHardwareMode_MyIDE_D1xx);
			CheckButton(IDC_IDE_D5XX, hwmode == kATIDEHardwareMode_MyIDE_D5xx);
			CheckButton(IDC_IDE_KMKJZV1, hwmode == kATIDEHardwareMode_KMKJZ_V1);
			CheckButton(IDC_IDE_KMKJZV2, hwmode == kATIDEHardwareMode_KMKJZ_V2);
			CheckButton(IDC_IDE_SIDE, hwmode == kATIDEHardwareMode_SIDE);

			bool fast = ide->IsFastDevice();
			CheckButton(IDC_SPEED_FAST, fast);
			CheckButton(IDC_SPEED_SLOW, !fast);
		} else {
			CheckButton(IDC_IDE_D5XX, true);
			CheckButton(IDC_SPEED_FAST, true);
		}

		UpdateEnables();
	} else {
		bool enable = IsButtonChecked(IDC_ENABLE);
		bool reset = false;

		ATIDEEmulator *ide = g_sim.GetIDEEmulator();
		if (IsButtonChecked(IDC_IDE_ENABLE)) {
			ATIDEHardwareMode hwmode = kATIDEHardwareMode_MyIDE_D5xx;
			if (IsButtonChecked(IDC_IDE_D1XX))
				hwmode = kATIDEHardwareMode_MyIDE_D1xx;
			else if (IsButtonChecked(IDC_IDE_KMKJZV1))
				hwmode = kATIDEHardwareMode_KMKJZ_V1;
			else if (IsButtonChecked(IDC_IDE_KMKJZV2))
				hwmode = kATIDEHardwareMode_KMKJZ_V2;
			else if (IsButtonChecked(IDC_IDE_SIDE))
				hwmode = kATIDEHardwareMode_SIDE;

			const bool write = !IsButtonChecked(IDC_IDEREADONLY);
			const bool fast = IsButtonChecked(IDC_SPEED_FAST);

			VDStringW path;
			GetControlText(IDC_IDE_IMAGEPATH, path);

			uint32 cylinders = 0;
			uint32 heads = 0;
			uint32 sectors = 0;

			ExchangeControlValueUint32(true, IDC_IDE_CYLINDERS, cylinders, 1, 16777216);
			ExchangeControlValueUint32(true, IDC_IDE_HEADS, heads, 1, 16);
			ExchangeControlValueUint32(true, IDC_IDE_SPT, sectors, 1, 255);

			if (!mbValidationFailed) {
				bool changed = true;

				if (ide) {
					changed = false;

					if (ide->GetImagePath() != path)
						changed = true;
					else if (ide->IsWriteEnabled() != write)
						changed = true;
					else if (g_sim.GetIDEHardwareMode() != hwmode)
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

				if (changed) {
					try {
						g_sim.LoadIDE(hwmode, write, fast, cylinders, heads, sectors, path.c_str());
						reset = true;
					} catch(const MyError& e) {
						e.post(mhdlg, "Altirra Error");
						FailValidation(IDC_IDE_IMAGEPATH);
						// fall through to force a reset
					}
				}
			}
		} else {
			if (ide) {
				g_sim.UnloadIDE();
				reset = true;
			}
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
				if (!s.empty())
					SetControlText(IDC_IDE_IMAGEPATH, s.c_str());
			}
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
					SetControlText(IDC_IDE_CYLINDERS, L"1");
					SetControlText(IDC_IDE_HEADS, L"15");
					SetControlText(IDC_IDE_SPT, L"255");
					UpdateCapacity();
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
	EnableControl(IDC_IDEREADONLY, ideenable);
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
	EnableControl(IDC_IDE_D1XX, ideenable);
	EnableControl(IDC_IDE_D5XX, ideenable);
	EnableControl(IDC_IDE_KMKJZV1, ideenable);
	EnableControl(IDC_IDE_KMKJZV2, ideenable);
	EnableControl(IDC_IDE_SIDE, ideenable);
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

void ATUIShowHardDiskDialog(VDGUIHandle hParent) {
	ATUIDialogHardDisk dlg;

	dlg.ShowDialog(hParent);
}
