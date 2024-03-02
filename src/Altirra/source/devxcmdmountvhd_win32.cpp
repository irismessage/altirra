//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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
#define INITGUID
#include <vd2/system/w32assist.h>
#include <windows.h>
#include <virtdisk.h>
#include "oshelper.h"
#include "uicommondialogs.h"

bool ATUIIsElevationRequiredForMountVHDImage() {
	bool hasTokenPermission = false;

	// Check if the process token has the SeManageVolumeName privilege. Note that it
	// doesn't need to be enabled, just present so it can be enabled.
	HANDLE hProcessToken;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hProcessToken)) {
		LUID permLuid {};
		if (LookupPrivilegeValue(nullptr, SE_MANAGE_VOLUME_NAME, &permLuid)) {
			// PrivilegeCheck() only works on impersonation tokens, so fetch the whole permissions
			// list instead. On Windows 10, the unelevated token normally has around 5 permissions,
			// and the elevated token 24.
			vdstructex<TOKEN_PRIVILEGES> privs;
			privs.resize(sizeof(TOKEN_PRIVILEGES) + sizeof(LUID_AND_ATTRIBUTES) * (50 - ANYSIZE_ARRAY));
			privs->PrivilegeCount = 50;

			for(int i=0; i<10; ++i) {
				DWORD sizeNeeded = 0;
				if (GetTokenInformation(hProcessToken, TokenPrivileges, privs.data(), privs.size(), &sizeNeeded)) {
					for(DWORD j = 0; j < privs->PrivilegeCount; ++j) {
						// let's try to be strict aliasing safe
						const LUID_AND_ATTRIBUTES& laa = *(const LUID_AND_ATTRIBUTES *)((uintptr)&privs->Privileges[0] + sizeof(LUID_AND_ATTRIBUTES) * j);

						if (laa.Luid.LowPart == permLuid.LowPart && laa.Luid.HighPart == permLuid.HighPart) {
							hasTokenPermission = true;
							break;
						}
					}

					break;
				}

				if (sizeNeeded < privs.size())
					break;

				privs.resize(sizeNeeded);
			}
		}

		CloseHandle(hProcessToken);
	}

	return !hasTokenPermission;
}

void ATUITemporarilyMountVHDImageImplW32(VDZHWND hwnd, const wchar_t *path, bool readOnly) {
	HMODULE hmodVirtDisk = VDLoadSystemLibraryW32("virtdisk");

	if (hmodVirtDisk) {
		auto openVirtDisk = (decltype(&OpenVirtualDisk))GetProcAddress(hmodVirtDisk, "OpenVirtualDisk");
		auto attachVirtDisk = (decltype(&AttachVirtualDisk))GetProcAddress(hmodVirtDisk, "AttachVirtualDisk");
		auto detachVirtDisk = (decltype(&DetachVirtualDisk))GetProcAddress(hmodVirtDisk, "DetachVirtualDisk");

		if (openVirtDisk && attachVirtDisk && detachVirtDisk) {
			HANDLE h = INVALID_HANDLE_VALUE;
			VIRTUAL_STORAGE_TYPE vs { VIRTUAL_STORAGE_TYPE_DEVICE_VHD, VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT };

			DWORD err = openVirtDisk(
				&vs,
				path,
				readOnly ? VIRTUAL_DISK_ACCESS_ATTACH_RO | VIRTUAL_DISK_ACCESS_DETACH | VIRTUAL_DISK_ACCESS_GET_INFO | VIRTUAL_DISK_ACCESS_READ
						: VIRTUAL_DISK_ACCESS_ALL,
				OPEN_VIRTUAL_DISK_FLAG_NONE,
				nullptr,
				&h);

			if (err == ERROR_SUCCESS) {
				err = attachVirtDisk(h, nullptr, ATTACH_VIRTUAL_DISK_FLAG_NONE, 0, nullptr, nullptr);
				if (err == ERROR_SUCCESS) {
					ATUIShowInfo((VDGUIHandle)hwnd, L"VHD has been mounted in Windows. Click OK when done to unmount.");

					detachVirtDisk(h, DETACH_VIRTUAL_DISK_FLAG_NONE, 0);
				}

				CloseHandle(h);
			}

			if (err != ERROR_SUCCESS) {
				LPTSTR msg = nullptr;

				(void)FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
					0,
					err,
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					(LPTSTR)&msg,
					1,
					NULL);

				VDStringW s;
				s.sprintf(L"Unable to mount VHD image: %s", msg ? msg : L"Unknown error.");
				
				ATUIShowError((VDGUIHandle)hwnd, s.c_str());

				if (msg)
					LocalFree(msg);
			}
		}

		FreeLibrary(hmodVirtDisk);
	}
}

void ATUITemporarilyMountVHDImageW32(VDGUIHandle parent, const wchar_t *path, bool readOnly) {
	if (ATUIIsElevationRequiredForMountVHDImage()) {
		if (parent)
			EnableWindow((HWND)parent, FALSE);

		VDStringW parentWindowStr;
		parentWindowStr.sprintf(L"0x%llX", (unsigned long long)parent);

		const wchar_t *args[] = {
			readOnly ? L"/mountvhdro" : L"/mountvhdrw",
			path,
			L"/parentwindow",
			parentWindowStr.c_str()
		};

		ATRelaunchElevatedWithEscapedArgs(parent, vdspan(args));

		if (parent)
			EnableWindow((HWND)parent, TRUE);
		return;
	}

	ATUITemporarilyMountVHDImageImplW32((VDZHWND)parent, path, readOnly);
}
