#include "stdafx.h"
#include "oshelper.h"
#include <windows.h>
#include <vd2/system/error.h>
#include <vd2/system/registry.h>
#include <vd2/system/w32assist.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Riza/bitmap.h>

bool ATLoadKernelResource(int id, void *dst, uint32 offset, uint32 size) {
	HMODULE hmod = VDGetLocalModuleHandleW32();

	HRSRC hrsrc = FindResourceA(hmod, MAKEINTRESOURCE(id), "KERNEL");
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
