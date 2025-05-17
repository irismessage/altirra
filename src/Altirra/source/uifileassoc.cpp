//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2011 Avery Lee
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

#include <stdafx.h>
#define INITGUID
#include <vd2/system/filesys.h>
#include <vd2/system/process.h>
#include <vd2/system/w32assist.h>
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include "resource.h"
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/uiproxies.h>
#include "oshelper.h"

///////////////////////////////////////////////////////////////////////////
// Registry utilities
//
// We need these because file associations need to be set in the Windows
// Registry regardless of whether our own 'registry' helpers are using
// the Registry or an INI file.
//
namespace {
	bool IsRegistryKeyPresent(HKEY hkey, const wchar_t *subKeyPath) {
		HKEY hSubKey = nullptr;
		if (ERROR_SUCCESS != RegOpenKeyExW(hkey, subKeyPath, 0, KEY_READ, &hSubKey))
			return false;

		RegCloseKey(hSubKey);
		return true;
	}
	
	bool WriteRegistryStringValue(HKEY hkey, const wchar_t *valueName, const wchar_t *s) {
		return ERROR_SUCCESS == RegSetValueExW(hkey, valueName, 0, REG_SZ, (const BYTE *)s, (DWORD)((wcslen(s) + 1) * sizeof(WCHAR)));
	}
}

///////////////////////////////////////////////////////////////////////////

struct ATFileAssociation {
	const wchar_t *mpExtList;
	const wchar_t *mpProgId;
	const wchar_t *mpDesc;
	const wchar_t *mpCommand;
	UINT mIconId;
} g_ATFileAssociations[]={
	{
		L"xex",
		L"Altirra.bin.1",
		L"Atari 8-bit Executable",
		L"/launch /run \"%1\"",
		IDI_XEX
	},
	{
		L"bin|car|rom|a52",
		L"Altirra.crt.1",
		L"Atari 8-bit Cartridge Image",
		L"/launch /cart \"%1\"",
		IDI_CART
	},
	{
		L"atr|dcm|atz|xfd|pro|atx",
		L"Altirra.dsk.1",
		L"Atari 8-bit Disk Image",
		L"/launch /disk \"%1\"",
		IDI_DISK
	},
	{
		L"cas",
		L"Altirra.tap.1",
		L"Atari 8-bit Tape Image",
		L"/launch /tape \"%1\"",
		IDI_TAPE
	},
};

// Remove the default application entries that the shell might have added due to
// manual registration. By default, if the program is chosen directly by the user,
// it will have an entry in SOFTWARE\Classes\Applications\exename.exe. We need to
// remove this as it will show up as a confusing duplicate in the Open With list
// that doesn't have the /launch option. This is created on a per-user basis, so
// it will be under HKCU.
void ATUnregisterPlaceholderAppEntry() {
	SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Applications\\Altirra.exe");
	SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Applications\\Altirra64.exe");
	SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Applications\\AltirraARM64.exe");
}

void ATRegisterForDefaultPrograms(bool userOnly) {
	const HKEY hkeyRoot = userOnly ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
	HKEY hkey;

	LONG err = RegCreateKeyExW(hkeyRoot, L"SOFTWARE\\RegisteredApplications", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hkey, NULL);
	if (err == ERROR_SUCCESS) {
		WriteRegistryStringValue(hkey, L"Altirra", L"Software\\virtualdub.org\\Altirra\\Capabilities");
		RegCloseKey(hkey);
	}

	err = RegCreateKeyExW(hkeyRoot, L"Software\\virtualdub.org\\Altirra\\Capabilities", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hkey, NULL);
	if (err == ERROR_SUCCESS) {
		WriteRegistryStringValue(hkey, L"ApplicationDescription", L"Altirra 8-bit computer emulator");

		HKEY hkey2;
		err = RegCreateKeyExW(hkey, L"FileAssociations", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hkey2, NULL);
		if (err == ERROR_SUCCESS) {
			for(const ATFileAssociation& assoc : g_ATFileAssociations) {
				VDStringRefW exts(assoc.mpExtList);
				VDStringRefW ext;

				while(!exts.empty()) {
					if (!exts.split('|', ext)) {
						ext = exts;
						exts.clear();
					}

					VDStringW keyName(L".");
					keyName += ext;
					WriteRegistryStringValue(hkey2, keyName.c_str(), assoc.mpProgId);
				}
			}

			RegCloseKey(hkey2);
		}

		RegCloseKey(hkey);
	}
}

void ATCreateFileAssociationProgId(const ATFileAssociation& assoc, bool userOnly) {
	HKEY hkey = nullptr;
	LONG err = RegCreateKeyExW(userOnly ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE, (VDStringW(L"Software\\Classes\\") + assoc.mpProgId).c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hkey, NULL);
	if (err == ERROR_SUCCESS) {
		// set file type description
		RegSetValueExW(hkey, NULL, 0, REG_SZ, (const BYTE *)assoc.mpDesc, (DWORD)((wcslen(assoc.mpDesc) + 1) * sizeof(wchar_t)));

		// set default icon
		HKEY hkey2;
		err = RegCreateKeyExW(hkey, L"DefaultIcon", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hkey2, NULL);
		if (err == ERROR_SUCCESS) {
			VDStringW iconPath;
			iconPath.sprintf(L"%ls,-%u", VDGetProgramFilePath().c_str(), assoc.mIconId);
			RegSetValueExW(hkey2, NULL, 0, REG_SZ, (const BYTE *)iconPath.c_str(), (iconPath.size() + 1) * sizeof(wchar_t));

			RegCloseKey(hkey2);
		}

		// Annoyingly, ApplicationCompany and FriendlyAppName are required for user-local registrations to
		// work. They are not necessary for system-wide registrations. If they are missing, LaunchAdvancedAssociationUI()
		// will fail with an error.
		err = RegCreateKeyExW(hkey, L"Application", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hkey2, NULL);
		if (err == ERROR_SUCCESS) {
			WriteRegistryStringValue(hkey2, L"ApplicationCompany", L"virtualdub.org");
			RegCloseKey(hkey2);
		}

		err = RegCreateKeyExW(hkey, L"shell\\open", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hkey2, NULL);
		if (err == ERROR_SUCCESS) {
			WriteRegistryStringValue(hkey2, L"FriendlyAppName", L"Altirra");
			RegCloseKey(hkey2);
		}

		err = RegCreateKeyExW(hkey, L"shell\\open\\command", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hkey2, NULL);
		if (err == ERROR_SUCCESS) {
			const VDStringW& programPath = VDGetProgramFilePath();

			VDStringW cmdLine;
			cmdLine = L"\"";
			cmdLine += programPath;
			cmdLine += L"\" ";
			cmdLine += assoc.mpCommand;

			RegSetValueExW(hkey2, NULL, 0, REG_SZ, (const BYTE *)cmdLine.c_str(), (cmdLine.size() + 1) * sizeof(wchar_t));

			RegCloseKey(hkey2);
		}

		RegCloseKey(hkey);
	}
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogFileAssociations2 : public VDDialogFrameW32 {
public:
	ATUIDialogFileAssociations2(bool userOnly);

protected:
	bool OnLoaded() override;
	bool OnCommand(uint32 id, uint32 extcode) override;

	bool mbUserOnly = false;
};

ATUIDialogFileAssociations2::ATUIDialogFileAssociations2(bool userOnly)
	: VDDialogFrameW32(IDD_FILE_ASSOC2)
	, mbUserOnly(userOnly)
{
}

bool ATUIDialogFileAssociations2::OnLoaded() {
	// We can't have this in the dialog template due to a 256 char limit imposed by RC.
	SetControlText(IDC_STATIC_MESSAGE,
		L"File type registration completed. This program can now be selected for supported file types in "
		L"Open With dialogs in the system.\n"
		L"\n"
		L"If you would like to pre-select this program for file types, this can be done in Windows Settings under "
		L"Default Apps. Starting with Windows 10, this requires several manual steps. Choose \"Set defaults by app\" "
		L"at the bottom, and select Altirra from the app list to pre-select it for file types."
	);

	return VDDialogFrameW32::OnLoaded();
}

bool ATUIDialogFileAssociations2::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_DEFAULTAPPS) {
		// As of Windows 11 21H2/22H2 w/ 2023-04 Cumulative Update, it is now possible once
		// again to specify the app in the URL.
		VDStringW s;
		s.sprintf(L"ms-settings:defaultapps?registeredApp%s=Altirra", mbUserOnly ? L"User" : L"Machine");
		ATLaunchURL(s.c_str());
		return true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////

void ATRegisterFileAssociations(bool allowElevation, bool userOnly) {
	if (!userOnly && allowElevation && !ATIsUserAdministrator()) {
		ATRelaunchElevated(nullptr, L"/registersysfileassocs /noelevation");
		return;
	}

	ATRegisterForDefaultPrograms(userOnly);

	for(const auto& assoc : g_ATFileAssociations)
		ATCreateFileAssociationProgId(assoc, userOnly);

	// Remove application registrations, in case the shell added them manually. This
	// is needed to avoid confusion between the 'proper' registrations we just added
	// and any manual ones that might have already existed. We remove them for all
	// variants if present.
	ATUnregisterPlaceholderAppEntry();

	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}

void ATUIShowDialogFileAssociationsVista(VDGUIHandle parent, bool userOnly) {
	// If we are on Windows 10 or higher, LaunchAdvancedAssociationUI() is useless because all
	// it does is display a dialog box telling the user to go to Settings. Worse, the user
	// can't refer to the dialog because it is modal, and it doesn't mention that the user
	// needs to click on the small Set Defaults By App link at the bottom, which can be missed
	// if the window is small enough to require scrolling.
	//
	// This annoying change dropped late in the Windows 10 Insider Preview cycle and persists
	// through Threshold 2, and to this date (Nov 2015) no documentation has actually been
	// released about how desktop programs are supposed to handle this, besides a patronizing
	// Windows build 10122 announcement that says that programs will need to be updated to
	// the Windows 10 way of handling file associations, without actually having any info
	// for developers describing what that is. /rant
	//
	// To work around this brain damaged UI flow while still trying to do "the right thing,"
	// we used to launch the Default Programs UI directly, which is all that the Settings link
	// did anyway, and which was still accessible through Control Panel. The user still had
	// to choose the application from the list, but the procedure to do so was directly
	// apparent. This unfortunately no longer works in 1803+, which simply just relaunches
	// Settings from the control panel, so for 1803+ we just launch the Settings page directly
	// to avoid the window pop.
	//
	// Note that in order to properly detect Windows 10, the application manifest must be
	// marked Win10-compatible.
	//

	if (VDIsAtLeast10W32()) {
		if (VDIsAtLeast10_1803W32()) {
			ATUIDialogFileAssociations2 dlg(userOnly);
			dlg.ShowDialog(parent);
		} else {
			VDLaunchProgram(VDMakePath(VDGetSystemPath().c_str(), L"control.exe").c_str(),
				L"/name Microsoft.DefaultPrograms /page pageDefaultProgram");
		}
	} else {
		IApplicationAssociationRegistrationUI *pAARegUI = nullptr;
		HRESULT hr = CoCreateInstance(CLSID_ApplicationAssociationRegistrationUI, nullptr,
			CLSCTX_INPROC, IID_IApplicationAssociationRegistrationUI, (void **)&pAARegUI);

		if (FAILED(hr))
			return;

		pAARegUI->LaunchAdvancedAssociationUI(L"Altirra");
		pAARegUI->Release();
	}
}

///////////////////////////////////////////////////////////////////////////

void ATUIShowDialogSetFileAssociations(VDGUIHandle parent, bool allowElevation, bool userOnly) {
	if (!userOnly && !ATIsUserAdministrator() && allowElevation) {
		VDStringW params;
		params.sprintf(L"/showfileassocdlg %llx", (unsigned long long)(uintptr_t)parent);

		ATRelaunchElevated(parent, params.c_str());
		return;
	}

	ATRegisterFileAssociations(false, userOnly);
	ATUIShowDialogFileAssociationsVista(parent, userOnly);
}

void ATUnregisterFileAssociations(bool userOnly) {
	const HKEY hkey = userOnly ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;

	// Remove Default Programs registration
	SHDeleteValueW(hkey, L"SOFTWARE\\RegisteredApplications", L"Altirra");

	// Remove local machine registration
	SHDeleteKeyW(hkey, L"Software\\virtualdub.org\\Altirra\\Capabilities");
	SHDeleteEmptyKeyW(hkey, L"Software\\virtualdub.org\\Altirra");

	// Remove all ProgIDs. We purposely leave the extension references dangling as
	// that is recommended by Microsoft -- the shell handles this.
	for(const ATFileAssociation& assoc : g_ATFileAssociations) {
		SHDeleteKeyW(hkey, (VDStringW(L"Software\\Classes\\") + assoc.mpProgId).c_str());
	}

	// Remove application registrations, in case the shell added them manually. We
	// remove them for all variants if present.
	ATUnregisterPlaceholderAppEntry();

	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}

void ATUIShowDialogRemoveFileAssociations(VDGUIHandle parent, bool allowElevation, bool userOnly) {
	if (!userOnly && allowElevation) {
		if (!ATIsUserAdministrator()) {
			VDStringW params;
			params.sprintf(L"/removefileassocs %llx", (unsigned long long)(uintptr_t)parent);

			ATRelaunchElevated(parent, params.c_str());
			return;
		}
	}

	if (IDOK != MessageBoxW((HWND)parent, L"Are you sure you want to disable all of this program's file associations?", L"Altirra Warning", MB_ICONEXCLAMATION | MB_OKCANCEL))
		return;

	ATUnregisterFileAssociations(userOnly);

	MessageBoxW((HWND)parent, L"File associations removed.", L"Altirra", MB_ICONINFORMATION | MB_OK);
}

void ATRemoveAllFileAssociations(bool allowElevation) {
	// remove per-user registrations
	ATUnregisterFileAssociations(true);

	// check for system-wide registrations
	bool sysPresent = IsRegistryKeyPresent(HKEY_LOCAL_MACHINE, L"Software\\RegisteredApplications\\Altirra");

	if (!sysPresent) {
		for(const auto& assoc : g_ATFileAssociations) {
			VDStringW subKey;

			subKey = L"Software\\Classes\\";
			subKey += assoc.mpProgId;

			if (IsRegistryKeyPresent(HKEY_LOCAL_MACHINE, subKey.c_str())) {
				sysPresent = true;
				break;
			}
		}
	}

	if (sysPresent) {
		if (allowElevation && !ATIsUserAdministrator())
			ATRelaunchElevated(nullptr, L"/unregisterfileassocs /noelevation");
		else
			ATUnregisterFileAssociations(false);
	}
}
