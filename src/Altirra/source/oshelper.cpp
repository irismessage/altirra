#include "stdafx.h"
#include "oshelper.h"
#include <windows.h>
#include <shlwapi.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/registry.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/w32assist.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Riza/bitmap.h>
#include "encode_png.h"

bool ATLoadKernelResource(int id, void *dst, uint32 offset, uint32 size) {
	HMODULE hmod = VDGetLocalModuleHandleW32();

	HRSRC hrsrc = FindResourceA(hmod, MAKEINTRESOURCEA(id), "KERNEL");
	if (!hrsrc)
		return false;

	DWORD rsize = SizeofResource(hmod, hrsrc);
	if (offset > rsize || (rsize - offset) < size)
		return false;

	HGLOBAL hg = LoadResource(hmod, hrsrc);

	const void *p = LockResource(hg);

	if (!p)
		return false;

	memcpy(dst, (const char *)p + offset, size);

	return true;
}

bool ATLoadKernelResourceLZPacked(int id, vdfastvector<uint8>& data) {
	HMODULE hmod = VDGetLocalModuleHandleW32();

	HRSRC hrsrc = FindResourceA(hmod, MAKEINTRESOURCEA(id), "KERNEL");
	if (!hrsrc)
		return false;

	HGLOBAL hg = LoadResource(hmod, hrsrc);
	const void *p = LockResource(hg);

	if (!p)
		return false;

	uint32 len = VDReadUnalignedLEU32(p);

	data.clear();
	data.resize(len);

	uint8 *dst = data.data();
	const uint8 *src = (const uint8 *)p + 4;

	for(;;) {
		uint8 c = *src++;

		if (!c)
			break;

		if (c & 1) {
			int distm1 = *src++;
			int len;

			if (c & 2) {
				distm1 += (c & 0xfc) << 6;
				len = *src++;
			} else {
				distm1 += ((c & 0x1c) << 6);
				len = c >> 5;
			}

			len += 3;

			const uint8 *csrc = dst - distm1 - 1;

			do {
				*dst++ = *csrc++;
			} while(--len);
		} else {
			c >>= 1;

			memcpy(dst, src, c);
			src += c;
			dst += c;
		}
	}

	return true;
}

bool ATLoadMiscResource(int id, vdfastvector<uint8>& data) {
	HMODULE hmod = VDGetLocalModuleHandleW32();

	HRSRC hrsrc = FindResourceA(hmod, MAKEINTRESOURCEA(id), "STUFF");
	if (!hrsrc)
		return false;

	DWORD rsize = SizeofResource(hmod, hrsrc);
	HGLOBAL hg = LoadResource(hmod, hrsrc);
	const void *p = LockResource(hg);

	if (!p)
		return false;

	data.resize(rsize);

	memcpy(data.data(), p, rsize);

	return true;
}

void ATFileSetReadOnlyAttribute(const wchar_t *path, bool readOnly) {
	VDStringA s;
	DWORD attrs;

	if (!VDIsWindowsNT()) {
		s = VDTextWToA(path);
		attrs = GetFileAttributesA(s.c_str());
	} else {
		attrs = GetFileAttributesW(path);
	}

	if (attrs == INVALID_FILE_ATTRIBUTES)
		throw MyWin32Error("Unable to change read-only flag on file: %s", GetLastError());

	if (readOnly)
		attrs |= FILE_ATTRIBUTE_READONLY;
	else
		attrs &= ~FILE_ATTRIBUTE_READONLY;

	BOOL success;
	if (!VDIsWindowsNT())
		success = SetFileAttributesA(s.c_str(), attrs);
	else
		success = SetFileAttributesW(path, attrs);

	if (!success)
		throw MyWin32Error("Unable to change read-only flag on file: %s", GetLastError());
}

void ATCopyFrameToClipboard(void *hwnd, const VDPixmap& px) {
	if (::OpenClipboard((HWND)hwnd)) {
		if (::EmptyClipboard()) {
			HANDLE hMem;
			void *lpvMem;

			VDPixmapLayout layout;
			uint32 imageSize = VDMakeBitmapCompatiblePixmapLayout(layout, px.w, px.h, nsVDPixmap::kPixFormat_RGB888, 0);

			vdstructex<VDAVIBitmapInfoHeader> bih;
			VDMakeBitmapFormatFromPixmapFormat(bih, nsVDPixmap::kPixFormat_RGB888, 0, px.w, px.h);

			uint32 headerSize = bih.size();

			if (hMem = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, headerSize + imageSize)) {
				if (lpvMem = ::GlobalLock(hMem)) {
					memcpy(lpvMem, bih.data(), headerSize);

					VDPixmapBlt(VDPixmapFromLayout(layout, (char *)lpvMem + headerSize), px);

					::GlobalUnlock(lpvMem);
					::SetClipboardData(CF_DIB, hMem);
					::CloseClipboard();
					return;
				}
				::GlobalFree(hMem);
			}
		}
		::CloseClipboard();
	}
}

void ATSaveFrame(void *hwnd, const VDPixmap& px, const wchar_t *filename) {
	VDPixmapBuffer pxbuf(px.w, px.h, nsVDPixmap::kPixFormat_RGB888);

	VDPixmapBlt(pxbuf, px);

	vdautoptr<IVDImageEncoderPNG> encoder(VDCreateImageEncoderPNG());
	const void *mem;
	uint32 len;
	encoder->Encode(pxbuf, mem, len, false);

	VDFile f(filename, nsVDFile::kWrite | nsVDFile::kDenyRead | nsVDFile::kCreateAlways);

	f.write(mem, len);
}

void ATCopyTextToClipboard(void *hwnd, const char *s) {
	if (::OpenClipboard((HWND)hwnd)) {
		if (::EmptyClipboard()) {
			HANDLE hMem;
			void *lpvMem;

			size_t len = strlen(s);

			if (hMem = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len + 1)) {
				if (lpvMem = ::GlobalLock(hMem)) {
					memcpy(lpvMem, s, len + 1);

					::GlobalUnlock(lpvMem);
					::SetClipboardData(CF_TEXT, hMem);
					::CloseClipboard();
					return;
				}
				::GlobalFree(hMem);
			}
		}
		::CloseClipboard();
	}
}

namespace {
	struct ATUISavedWindowPlacement {
		sint32 mLeft;
		sint32 mTop;
		sint32 mRight;
		sint32 mBottom;
		uint8 mbMaximized;
		uint8 mPad[3];
	};
}

void ATUISaveWindowPlacement(void *hwnd, const char *name) {
	VDRegistryAppKey key("Window Placement");

	WINDOWPLACEMENT wp = {sizeof(WINDOWPLACEMENT)};

	if (GetWindowPlacement((HWND)hwnd, &wp)) {
		ATUISavedWindowPlacement sp = {0};
		sp.mLeft	= wp.rcNormalPosition.left;
		sp.mTop		= wp.rcNormalPosition.top;
		sp.mRight	= wp.rcNormalPosition.right;
		sp.mBottom	= wp.rcNormalPosition.bottom;
		sp.mbMaximized = (wp.showCmd == SW_MAXIMIZE);
		key.setBinary(name, (const char *)&sp, sizeof sp);
	}
}

void ATUIRestoreWindowPlacement(void *hwnd, const char *name, int nCmdShow) {
	if (!IsZoomed((HWND)hwnd) && !IsIconic((HWND)hwnd)) {
		VDRegistryAppKey key("Window Placement");
		ATUISavedWindowPlacement sp = {0};

		// Earlier versions only saved a RECT.
		int len = key.getBinaryLength(name);

		if (len > (int)sizeof(ATUISavedWindowPlacement))
			len = sizeof(ATUISavedWindowPlacement);

		if (len >= offsetof(ATUISavedWindowPlacement, mbMaximized) && key.getBinary(name, (char *)&sp, len)) {
			WINDOWPLACEMENT wp = {sizeof(WINDOWPLACEMENT)};

			if (GetWindowPlacement((HWND)hwnd, &wp)) {
				wp.length			= sizeof(WINDOWPLACEMENT);
				wp.flags			= 0;
				wp.showCmd			= nCmdShow;
				wp.rcNormalPosition.left = sp.mLeft;
				wp.rcNormalPosition.top = sp.mTop;
				wp.rcNormalPosition.right = sp.mRight;
				wp.rcNormalPosition.bottom = sp.mBottom;

				if ((wp.showCmd == SW_SHOW || wp.showCmd == SW_SHOWNORMAL || wp.showCmd == SW_SHOWDEFAULT) && sp.mbMaximized)
					wp.showCmd = SW_SHOWMAXIMIZED;

				SetWindowPlacement((HWND)hwnd, &wp);
			}
		}
	}
}

void ATUIEnableEditControlAutoComplete(void *hwnd) {
	if (hwnd)
		SHAutoComplete((HWND)hwnd, SHACF_FILESYSTEM | SHACF_AUTOAPPEND_FORCE_OFF);
}

VDStringW ATGetHelpPath() {
	return VDMakePath(VDGetProgramPath().c_str(), L"Altirra.chm");
}

void ATShowHelp(void *hwnd, const wchar_t *filename) {
	try {
		VDStringW helpFile(ATGetHelpPath());

		if (!VDDoesPathExist(helpFile.c_str()))
			throw MyError("Cannot find help file: %ls", helpFile.c_str());

		// If we're on Windows NT, check for the ADS and/or network drive.
		if (VDIsWindowsNT()) {
			VDStringW helpFileADS(helpFile);
			helpFileADS += L":Zone.Identifier";
			if (VDDoesPathExist(helpFileADS.c_str())) {
				int rv = MessageBox((HWND)hwnd, _T("Altirra has detected that its help file, Altirra.chm, has an Internet Explorer download location marker on it. This may prevent the help file from being displayed properly, resulting in \"Action canceled\" errors being displayed. Would you like to remove it?"), _T("Altirra warning"), MB_YESNO|MB_ICONEXCLAMATION);

				if (rv == IDYES)
					DeleteFileW(helpFileADS.c_str());
			}
		}

		if (filename) {
			helpFile.append(L"::/");
			helpFile.append(filename);
		}

		VDStringW helpCommand(VDStringW(L"\"hh.exe\" \"") + helpFile + L'"');

		PROCESS_INFORMATION pi;
		BOOL retval;

		// CreateProcess will actually modify the string that it gets, soo....
		if (VDIsWindowsNT()) {
			STARTUPINFOW si = {sizeof(STARTUPINFOW)};
			std::vector<wchar_t> tempbufW(helpCommand.size() + 1, 0);
			helpCommand.copy(&tempbufW[0], tempbufW.size());
			retval = CreateProcessW(NULL, &tempbufW[0], NULL, NULL, FALSE, CREATE_DEFAULT_ERROR_MODE, NULL, NULL, &si, &pi);
		} else {
			STARTUPINFOA si = {sizeof(STARTUPINFOA)};
			VDStringA strA(VDTextWToA(helpCommand));
			std::vector<char> tempbufA(strA.size() + 1, 0);
			strA.copy(&tempbufA[0], tempbufA.size());
			retval = CreateProcessA(NULL, &tempbufA[0], NULL, NULL, FALSE, CREATE_DEFAULT_ERROR_MODE, NULL, NULL, &si, &pi);
		}

		if (retval) {
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
		} else
			throw MyWin32Error("Cannot launch HTML Help: %%s", GetLastError());
	} catch(const MyError& e) {
		e.post((HWND)hwnd, "Altirra Error");
	}
}

bool ATIsUserAdministrator() {
	if (LOBYTE(LOWORD(::GetVersion())) < 6)
		return TRUE;

	BOOL isAdmin = FALSE;

	HMODULE hmodAdvApi = LoadLibraryW(L"advapi32");

	if (hmodAdvApi) {
		typedef BOOL (WINAPI *tpCreateWellKnownSid)(WELL_KNOWN_SID_TYPE WellKnownSidType, PSID DomainSid, PSID pSid, DWORD *cbSid);
		tpCreateWellKnownSid pCreateWellKnownSid = (tpCreateWellKnownSid)GetProcAddress(hmodAdvApi, "CreateWellKnownSid");

		if (pCreateWellKnownSid) {
			DWORD sidLen = SECURITY_MAX_SID_SIZE;
			BYTE localAdminsGroupSid[SECURITY_MAX_SID_SIZE];

			if (pCreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, localAdminsGroupSid, &sidLen)) {
				CheckTokenMembership(NULL, localAdminsGroupSid, &isAdmin);
			}
		}

		FreeLibrary(hmodAdvApi);
	}

	return isAdmin != 0;
}

void ATGenerateGuid(uint8 rawguid[16]) {
	GUID guid = {0};
	CoCreateGuid(&guid);

	memcpy(rawguid, &guid, 16);
}
