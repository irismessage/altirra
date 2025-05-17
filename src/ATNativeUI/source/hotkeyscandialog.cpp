//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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

#include <windows.h>
#include <vd2/Dita/accel.h>
#include <at/atnativeui/dialog.h>
#include "resource.h"

///////////////////////////////////////////////////////////////////////////

class ATUIDialogScanHotKeys : public VDDialogFrameW32 {
public:
	ATUIDialogScanHotKeys();
	~ATUIDialogScanHotKeys();

protected:
	bool OnLoaded();
	bool OnCommand(uint32 id, uint32 extcode);
	void DoScan();

	static const uint8 kVKeys[];
};

constexpr uint8 ATUIDialogScanHotKeys::kVKeys[] {
	VK_BACK,
	VK_TAB,
	VK_CLEAR,
	VK_RETURN,
	VK_ESCAPE,
	VK_SPACE,
	VK_PRIOR,
	VK_NEXT,
	VK_END,
	VK_HOME,
	VK_LEFT,
	VK_UP,
	VK_RIGHT,
	VK_DOWN,
	VK_INSERT,
	VK_DELETE,
	0x30,
	0x31,
	0x32,
	0x33,
	0x34,
	0x35,
	0x36,
	0x37,
	0x38,
	0x39,
	0x41,
	0x42,
	0x43,
	0x44,
	0x45,
	0x46,
	0x47,
	0x48,
	0x49,
	0x4A,
	0x4B,
	0x4C,
	0x4D,
	0x4E,
	0x4F,
	0x50,
	0x51,
	0x52,
	0x53,
	0x54,
	0x55,
	0x56,
	0x57,
	0x58,
	0x59,
	0x5A,
	VK_NUMPAD0,
	VK_NUMPAD1,
	VK_NUMPAD2,
	VK_NUMPAD3,
	VK_NUMPAD4,
	VK_NUMPAD5,
	VK_NUMPAD6,
	VK_NUMPAD7,
	VK_NUMPAD8,
	VK_NUMPAD9,
	VK_MULTIPLY,
	VK_ADD,
	VK_SEPARATOR,
	VK_SUBTRACT,
	VK_DECIMAL,
	VK_DIVIDE,
	VK_F1,
	VK_F2,
	VK_F3,
	VK_F4,
	VK_F5,
	VK_F6,
	VK_F7,
	VK_F8,
	VK_F9,
	VK_F10,
	VK_F11,
	VK_F12,
	VK_OEM_1,
	VK_OEM_PLUS,
	VK_OEM_COMMA,
	VK_OEM_MINUS,
	VK_OEM_PERIOD,
	VK_OEM_2,
	VK_OEM_3,
	VK_OEM_4,
	VK_OEM_5,
	VK_OEM_6,
	VK_OEM_7,
};

ATUIDialogScanHotKeys::ATUIDialogScanHotKeys()
	: VDDialogFrameW32(IDD_HOTKEY_SCAN)
{
}

ATUIDialogScanHotKeys::~ATUIDialogScanHotKeys() {
}

bool ATUIDialogScanHotKeys::OnLoaded() {
	mResizer.Add(IDOK, VDDialogResizerW32::kBR);
	mResizer.Add(IDC_SCAN, VDDialogResizerW32::kBR);
	mResizer.Add(IDC_REPORT, VDDialogResizerW32::kMC | VDDialogResizerW32::kAvoidFlicker);

	SetControlText(IDC_REPORT,
		L"This tool scans the computer for global hotkeys registered by other programs, "
		L"which makes those key combinations unavailable for this program. Press Scan to "
		L"check for global hotkeys."
	);

	SetFocusToControl(IDC_SCAN);
	return true;
}

bool ATUIDialogScanHotKeys::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_SCAN) {
		DoScan();
		return true;
	}

	return false;
}

void ATUIDialogScanHotKeys::DoScan() {
	VDStringW log;
	VDStringW accelStr;
	bool found = false;

	log = L"Global hotkeys registered by other programs:\r\n\r\n";

	for(int modIdx = 0; modIdx < 8; ++modIdx) {
		const bool alt = (modIdx & 4) != 0;
		const bool ctrl = (modIdx & 2) != 0;
		const bool shift = (modIdx & 1) != 0;

		VDUIAccelerator accel {};
		accel.mModifiers = 0;

		UINT mods = 0;
		if (alt) {
			mods += MOD_ALT;
			accel.mModifiers |= VDUIAccelerator::kModAlt;
		}

		if (shift) {
			mods += MOD_SHIFT;
			accel.mModifiers |= VDUIAccelerator::kModShift;
		}

		if (ctrl) {
			mods += MOD_CONTROL;
			accel.mModifiers |= VDUIAccelerator::kModCtrl;
		}

		for(const uint8 vk : kVKeys) {
			// skip keys that are reserved by windows
			switch(vk) {
				case VK_F4:
					// Alt+F4
					if (!ctrl && !shift && alt)
						continue;
					break;

				case VK_F12:
					// F12
					// Shift+F12
					if (!ctrl && !alt)
						continue;
					break;

				case VK_ESCAPE:
					// Ctrl+Esc
					// Ctrl+Shift+Esc
					if (ctrl && !alt)
						continue;
					break;

				case VK_DELETE:
					// Ctrl+Alt+Del
					if (ctrl && alt && !shift)
						continue;
					break;

				case VK_TAB:
					// Alt+Tab
					// Alt+Shift+Tab
					// Ctrl+Alt+Tab
					// Ctrl+Alt+Shift+Tab
					if (alt)
						continue;
					break;

				default:
					break;
			}

			if (RegisterHotKey(mhdlg, 0x1000, mods, vk)) {
				UnregisterHotKey(mhdlg, 0x1000);
			} else {
				accel.mVirtKey = vk;
				VDUIGetAcceleratorString(accel, accelStr);

				log += L"    ";
				log += accelStr;
				log += L"\r\n";
				found = true;
			}
		}
	}

	if (!found)
		log += L"None found.\r\n";

	SetControlText(IDC_REPORT, log.c_str());
}

///////////////////////////////////////////////////////////////////////////

void ATUIShowDialogScanHotKeys(VDGUIHandle hParent) {
	ATUIDialogScanHotKeys dlg;

	dlg.ShowDialog(hParent);
}
