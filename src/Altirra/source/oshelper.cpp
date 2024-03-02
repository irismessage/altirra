//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
#include "oshelper.h"
#include <windows.h>
#include <wincodec.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj_core.h>
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
#include <at/atnativeui/uiframe.h>
#include <at/atnativeui/theme.h>
#include "decode_png.h"
#include "encode_png.h"
#include "common_png.h"

const void *ATLockResource(uint32 id, size_t& size) {
	HMODULE hmod = VDGetLocalModuleHandleW32();

	HRSRC hrsrc = FindResourceA(hmod, MAKEINTRESOURCEA(id), "STUFF");
	if (!hrsrc)
		return nullptr;

	size = SizeofResource(hmod, hrsrc);

	HGLOBAL hg = LoadResource(hmod, hrsrc);
	const void *p = LockResource(hg);

	return p;
}

bool ATLoadKernelResource(int id, void *dst, uint32 offset, uint32 size, bool allowPartial) {
	HMODULE hmod = VDGetLocalModuleHandleW32();

	HRSRC hrsrc = FindResourceA(hmod, MAKEINTRESOURCEA(id), "KERNEL");
	if (!hrsrc)
		return false;

	DWORD rsize = SizeofResource(hmod, hrsrc);
	if (offset > rsize)
		return false;

	if ((rsize - offset) < size) {
		if (!allowPartial)
			return false;

		size = rsize - offset;
	}

	HGLOBAL hg = LoadResource(hmod, hrsrc);

	const void *p = LockResource(hg);

	if (!p)
		return false;

	memcpy(dst, (const char *)p + offset, size);

	return true;
}

bool ATLoadKernelResource(int id, vdfastvector<uint8>& buf) {
	HMODULE hmod = VDGetLocalModuleHandleW32();

	HRSRC hrsrc = FindResourceA(hmod, MAKEINTRESOURCEA(id), "KERNEL");
	if (!hrsrc)
		return false;

	DWORD rsize = SizeofResource(hmod, hrsrc);
	HGLOBAL hg = LoadResource(hmod, hrsrc);

	const uint8 *p = (const uint8 *)LockResource(hg);

	if (!p)
		return false;

	buf.assign(p, p + rsize);
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

bool ATLoadImageResource(uint32 id, VDPixmapBuffer& buf) {
	HMODULE hmod = VDGetLocalModuleHandleW32();

	HRSRC hrsrc = FindResourceW(hmod, MAKEINTRESOURCEW(id), L"PNG");
	if (!hrsrc)
		return false;

	DWORD rsize = SizeofResource(hmod, hrsrc);
	HGLOBAL hg = LoadResource(hmod, hrsrc);
	const void *p = LockResource(hg);

	if (!p)
		return false;

	vdautoptr<IVDImageDecoderPNG> decoder(VDCreateImageDecoderPNG());

	if (decoder->Decode(p, rsize))
		return false;

	buf.assign(decoder->GetFrameBuffer());
	return true;
}

void ATFileSetReadOnlyAttribute(const wchar_t *path, bool readOnly) {
	VDStringA s;
	DWORD attrs;

	attrs = GetFileAttributesW(path);

	if (attrs == INVALID_FILE_ATTRIBUTES)
		throw MyWin32Error("Unable to change read-only flag on file: %s", GetLastError());

	if (readOnly)
		attrs |= FILE_ATTRIBUTE_READONLY;
	else
		attrs &= ~FILE_ATTRIBUTE_READONLY;

	BOOL success = SetFileAttributesW(path, attrs);

	if (!success)
		throw MyWin32Error("Unable to change read-only flag on file: %s", GetLastError());
}

void ATCopyFrameToClipboard(const VDPixmap& px) {
	if (::OpenClipboard(nullptr)) {
		if (::EmptyClipboard()) {
			HANDLE hMem;
			void *lpvMem;

			VDPixmapLayout layout;
			uint32 imageSize = VDMakeBitmapCompatiblePixmapLayout(layout, px.w, px.h, nsVDPixmap::kPixFormat_RGB888, 0);

			vdstructex<VDAVIBitmapInfoHeader> bih;
			VDMakeBitmapFormatFromPixmapFormat(bih, nsVDPixmap::kPixFormat_RGB888, 0, px.w, px.h);

			uint32 headerSize = (uint32)bih.size();

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

void ATLoadFrame(VDPixmapBuffer& px, const wchar_t *filename) {
	VDFile f(filename);

	sint64 size = f.size();
	if (size > 256*1024*1024)
		throw MyError("File is too large to load.");

	vdblock<unsigned char> buf((uint32)size);
	f.read(buf.data(), (long)size);
	f.close();

	ATLoadFrameFromMemory(px, buf.data(), size);
}

void ATLoadFrameFromMemory(VDPixmapBuffer& px, const void *mem, size_t len) {
	uint8 buf8[4];
	memcpy(buf8, mem, 4);

	GUID containerFormat;

	if (len >= 4 && buf8[0] == 0xFF && buf8[1] == 0xD8 && buf8[2] == 0xFF && (buf8[3] & 0xF0) == 0xE0) {
		containerFormat = GUID_ContainerFormatJpeg;
	} else if (len >= 8 && !memcmp(mem, nsVDPNG::kPNGSignature, 8)){
		containerFormat = GUID_ContainerFormatPng;
	} else
		throw MyError("Unsupported image format.");

	vdrefptr<IWICImagingFactory> factory;
	vdrefptr<IWICStream> stream;
	vdrefptr<IWICBitmapDecoder> decoder;

	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_ALL, __uuidof(IWICImagingFactory), (void **)~factory))
		|| FAILED(factory->CreateStream(~stream))
		|| FAILED(stream->InitializeFromMemory((BYTE *)mem, (DWORD)len))
		|| FAILED(factory->CreateDecoder(containerFormat, &GUID_VendorMicrosoft, ~decoder))
	) {
		throw MyError("Unable to initialize Windows Imaging.");
	}

	HRESULT hr = decoder->Initialize(stream, WICDecodeMetadataCacheOnDemand);
	if (SUCCEEDED(hr)) {
		vdrefptr<IWICBitmapFrameDecode> frameDecode;
		hr = decoder->GetFrame(0, ~frameDecode);

		if (SUCCEEDED(hr)) {
			vdrefptr<IWICFormatConverter> formatConverter;

			hr = factory->CreateFormatConverter(~formatConverter);
			if (SUCCEEDED(hr)) {
				hr = formatConverter->Initialize(frameDecode, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.5, WICBitmapPaletteTypeMedianCut);

				if (SUCCEEDED(hr)) {
					UINT w = 0, h = 0;
					hr = formatConverter->GetSize(&w, &h);

					if (SUCCEEDED(hr) && w && h) {
						px.init(w, h, nsVDPixmap::kPixFormat_XRGB8888);

						hr = formatConverter->CopyPixels(nullptr, px.pitch, px.pitch * px.h, (BYTE *)px.data);
						if (SUCCEEDED(hr)) {
							return;
						}
					}
				}
			}
		}
	}

	throw MyError("Unable to decode image.");
}

void ATSaveFrame(const VDPixmap& px, const wchar_t *filename) {
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

void ATCopyTextToClipboard(void *hwnd, const wchar_t *s) {
	if (!::OpenClipboard((HWND)hwnd))
		return;

	if (::EmptyClipboard()) {
		HANDLE hMem;
		void *lpvMem;

		size_t len = wcslen(s);

		if (hMem = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (len + 1) * sizeof(WCHAR))) {
			if (lpvMem = ::GlobalLock(hMem)) {
				memcpy(lpvMem, s, (len + 1) * sizeof(WCHAR));

				::GlobalUnlock(lpvMem);
				::SetClipboardData(CF_UNICODETEXT, hMem);
				::CloseClipboard();
				return;
			}
			::GlobalFree(hMem);
		}
	}
	::CloseClipboard();
}

namespace {
	struct ATUISavedWindowPlacement {
		sint32 mLeft;
		sint32 mTop;
		sint32 mRight;
		sint32 mBottom;
		uint8 mbMaximized;
		uint8 mPad[3];
		uint32 mDpi;			// added - v3
	};
}

void ATUISaveWindowPlacement(void *hwnd, const char *name) {
	WINDOWPLACEMENT wp = {sizeof(WINDOWPLACEMENT)};

	if (GetWindowPlacement((HWND)hwnd, &wp)) {
		ATUISaveWindowPlacement(name,
			vdrect32 {
				wp.rcNormalPosition.left,
				wp.rcNormalPosition.top,
				wp.rcNormalPosition.right,
				wp.rcNormalPosition.bottom,
			},
			wp.showCmd == SW_MAXIMIZE,
			ATUIGetWindowDpiW32((HWND)hwnd));
	}
}

void ATUISaveWindowPlacement(const char *name, const vdrect32& r, bool isMaximized, uint32 dpi) {
	VDRegistryAppKey key("Window Placement");

	ATUISavedWindowPlacement sp {};
	sp.mLeft	= r.left;
	sp.mTop		= r.top;
	sp.mRight	= r.right;
	sp.mBottom	= r.bottom;
	sp.mbMaximized = isMaximized;
	sp.mDpi		= dpi;
	key.setBinary(name, (const char *)&sp, sizeof sp);
}

void ATUIRestoreWindowPlacement(void *hwnd, const char *name, int nCmdShow, bool sizeOnly) {
	if (nCmdShow < 0)
		nCmdShow = SW_SHOW;

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

				sint32 width = sp.mRight - sp.mLeft;
				sint32 height = sp.mBottom - sp.mTop;

				// If we have a DPI value, try to compensate for DPI differences.
				if (sp.mDpi) {
					// Obtain the primary work area.
					RECT rWorkArea = {};
					if (SystemParametersInfo(SPI_GETWORKAREA, 0, &rWorkArea, FALSE)) {
						// Translate rcNormalPosition to screen coordinates.
						RECT rScreen {
							wp.rcNormalPosition.left + rWorkArea.left,
							wp.rcNormalPosition.top + rWorkArea.top,
							wp.rcNormalPosition.right + rWorkArea.left,
							wp.rcNormalPosition.bottom + rWorkArea.top,
						};

						HMONITOR hMon = MonitorFromRect(&rScreen, MONITOR_DEFAULTTONEAREST);
						uint32 currentDpi = ATUIGetMonitorDpiW32(hMon);

						if (currentDpi) {
							const double dpiConversionFactor = (double)currentDpi / (double)sp.mDpi;
							width = VDRoundToInt32((double)width * dpiConversionFactor);
							height = VDRoundToInt32((double)height * dpiConversionFactor);
						}
					}
				}

				if (sizeOnly) {
					wp.rcNormalPosition.right = wp.rcNormalPosition.left + width;
					wp.rcNormalPosition.bottom = wp.rcNormalPosition.top + height;
				} else {
					wp.rcNormalPosition.left = sp.mLeft;
					wp.rcNormalPosition.top = sp.mTop;
					wp.rcNormalPosition.right = sp.mLeft + width;
					wp.rcNormalPosition.bottom = sp.mTop + height;
				}

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
		{
			VDStringW helpFileADS(helpFile);
			helpFileADS += L":Zone.Identifier";
			if (VDDoesPathExist(helpFileADS.c_str())) {
				int rv = MessageBox((HWND)hwnd, _T("Altirra has detected that its help file, Altirra.chm, has an Internet Explorer download location marker on it. This may prevent the help file from being displayed properly, resulting in \"Action canceled\" errors being displayed. Would you like to remove it?"), _T("Altirra warning"), MB_YESNO|MB_ICONEXCLAMATION);

				if (rv == IDYES)
					DeleteFileW(helpFileADS.c_str());
			}
		}

		const bool useDarkAnchor = (!filename || !wcschr(filename, L'#')) && ATUIIsDarkThemeActive();

		if (!filename && useDarkAnchor)
			filename = L"index.html";

		if (filename) {
			helpFile.append(L"::/");
			helpFile.append(filename);
		}

		if (useDarkAnchor)
			helpFile.append(L"#dark-theme");

		VDStringW helpCommand(VDStringW(L"\"hh.exe\" \"") + helpFile + L'"');

		PROCESS_INFORMATION pi;
		BOOL retval;

		// CreateProcess will actually modify the string that it gets, soo....
		{
			STARTUPINFOW si = {sizeof(STARTUPINFOW)};
			std::vector<wchar_t> tempbufW(helpCommand.size() + 1, 0);
			helpCommand.copy(&tempbufW[0], (uint32)tempbufW.size());
			retval = CreateProcessW(NULL, &tempbufW[0], NULL, NULL, FALSE, CREATE_DEFAULT_ERROR_MODE, NULL, NULL, &si, &pi);
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

void ATLaunchURL(const wchar_t *url) {
	const VDStringW path = VDGetProgramFilePath();

	SHELLEXECUTEINFOW execInfo = {sizeof(SHELLEXECUTEINFOW)};
	execInfo.lpVerb = nullptr;
	execInfo.lpFile = url;
	execInfo.nShow = SW_SHOWNORMAL;

	ShellExecuteExW(&execInfo);
}

bool ATIsUserAdministrator() {
	if (!VDIsAtLeastVistaW32())
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

void ATShowFileInSystemExplorer(const wchar_t *filename) {
	PIDLIST_ABSOLUTE pidl = ILCreateFromPath(VDGetFullPath(filename).c_str());
	if (pidl) {
		// This method is superior to launching explorer.exe with /select, because
		// that has an issue on Windows 10 with spawning the new Explorer window
		// in the background. Apparently with separate folder processes enabled
		// it doesn't properly call AllowSetForegroundWindow() on the new process.
		SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
		ILFree(pidl);
	}
}

void ATRelaunchElevated(VDGUIHandle parent, const wchar_t *params) {
	const VDStringW path = VDGetProgramFilePath();

	SHELLEXECUTEINFOW execInfo = {sizeof(SHELLEXECUTEINFOW)};
	execInfo.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_UNICODE | SEE_MASK_WAITFORINPUTIDLE;
	execInfo.hwnd = (HWND)parent;
	execInfo.lpVerb = L"runas";
	execInfo.lpFile = path.c_str();
	execInfo.lpParameters = params;
	execInfo.nShow = SW_SHOWNORMAL;

	if (ShellExecuteExW(&execInfo) && execInfo.hProcess) {
		for(;;) {
			DWORD r = MsgWaitForMultipleObjects(1, &execInfo.hProcess, FALSE, INFINITE, QS_ALLINPUT);

			if (r == WAIT_OBJECT_0 + 1) {
				MSG msg;
				while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
					if (CallMsgFilter(&msg, 0))
						continue;

					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}
			} else
				break;
		}

		CloseHandle(execInfo.hProcess);
	}
}

void ATRelaunchElevatedWithEscapedArgs(VDGUIHandle parent, vdspan<const wchar_t *> args) {
	VDStringW argStr;

	for(const wchar_t *s : args) {
		if (!argStr.empty())
			argStr += L' ';

		bool needsEscaping = false;
		if (*s != '/') {
			for(const wchar_t *t = s; *t; ++t) {
				if (*t == '\\' || *t == '/' || *t == '"') {
					needsEscaping = true;
					break;
				}
			}
		}

		if (needsEscaping) {
			argStr += L'"';

			for(const wchar_t *t = s; *t; ++t) {
				if (*t == '\\' || *t == '"')
					argStr += L'\\';

				argStr += *t;
			}

			argStr += L'"';
		} else {
			argStr += s;
		}
	}

	ATRelaunchElevated(parent, argStr.c_str());
}
