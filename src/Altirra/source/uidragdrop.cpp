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
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <vd2/system/atomic.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/w32assist.h>
#include "simulator.h"
#include "resource.h"

extern HWND g_hwnd;
extern ATSimulator g_sim;

extern void DoLoad(VDGUIHandle h, const wchar_t *path, bool vrw, bool rw, int cartmapper, ATLoadType loadType = kATLoadType_Other, bool *suppressColdReset = NULL, int loadIndex = -1);
extern void DoLoadStream(VDGUIHandle h, const wchar_t *fileName, IVDRandomAccessStream& stream, bool vrw, int cartmapper, ATLoadType loadType = kATLoadType_Other, bool *suppressColdReset = NULL, int loadIndex = -1);
extern void DoBootWithConfirm(const wchar_t *path, bool vrw, bool rw, int cartmapper);
extern void DoBootStreamWithConfirm(const wchar_t *fileName, IVDRandomAccessStream& stream, bool vrw, int cartmapper);

///////////////////////////////////////////////////////////////////////////////

void ATReadCOMStreamIntoMemory(vdfastvector<char>& data, IStream *stream) {
	char buf[65536];

	for(;;) {
		ULONG actual;
		HRESULT hr = stream->Read(buf, sizeof buf, &actual);

		if (FAILED(hr))
			throw MyError("An error was encountered while reading from the input stream.");

		if (hr != S_OK && hr != S_FALSE)
			break;
		
		if (64*1024*1024 - data.size() < actual)
			throw MyError("The dragged file is too large to load (>64MB).");

		data.insert(data.end(), buf, buf + actual);

		if (actual < sizeof buf || hr == S_FALSE)
			break;
	}
}

void ATReadCOMBufferIntoMemory(vdfastvector<char>& data, HGLOBAL hglobal, uint32 knownSize) {
	uint32 size = GlobalSize(hglobal);

	// the global buffer may be a bit big, so truncate it if we know the real
	// size
	if (knownSize && knownSize > size)
		size = knownSize;

	data.resize(size);

	// lock the memory block
	void *src = GlobalLock(hglobal);
	if (src) {
		memcpy(data.data(), src, size);
		GlobalUnlock(hglobal);
	}
}

///////////////////////////////////////////////////////////////////////////////

class ATUIDragDropHandler : public IDropTarget {
public:
	ATUIDragDropHandler(HWND hwnd);

	virtual ULONG STDMETHODCALLTYPE AddRef();
	virtual ULONG STDMETHODCALLTYPE Release();
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(const IID& riid, void **ppvObject);
    virtual HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);
    virtual HRESULT STDMETHODCALLTYPE DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);
    virtual HRESULT STDMETHODCALLTYPE DragLeave();
    virtual HRESULT STDMETHODCALLTYPE Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);

protected:
	bool GetLoadTarget(POINTL pt, ATLoadType& loadType, int& loadIndex, bool& doBoot) const;
	void SetDropEffect(DWORD grfKeyState);

	VDAtomicInt mRefCount;
	DWORD mDropEffect;
	bool mbOpenContextMenu;
	HWND mhwnd;
	UINT mClipFormatFileDescriptorA;
	UINT mClipFormatFileDescriptorW;
	UINT mClipFormatFileContents;
};

///////////////////////////////////////////////////////////////////////////////

ATUIDragDropHandler::ATUIDragDropHandler(HWND hwnd)
	: mRefCount(0)
	, mDropEffect(DROPEFFECT_NONE)
	, mhwnd(hwnd)
{
	mClipFormatFileDescriptorA = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORA);
	mClipFormatFileDescriptorW = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORW);
	mClipFormatFileContents = RegisterClipboardFormat(CFSTR_FILECONTENTS);
}

ULONG STDMETHODCALLTYPE ATUIDragDropHandler::AddRef() {
	return ++mRefCount;
}

ULONG STDMETHODCALLTYPE ATUIDragDropHandler::Release() {
	ULONG rc = --mRefCount;

	if (!rc)
		delete this;

	return rc;
}

HRESULT STDMETHODCALLTYPE ATUIDragDropHandler::QueryInterface(const IID& riid, void **ppvObject) {
	if (riid == IID_IUnknown) {
		*ppvObject = static_cast<IUnknown *>(this);
		AddRef();
		return S_OK;
	} else if (riid == IID_IDropTarget) {
		*ppvObject = static_cast<IDropTarget *>(this);
		AddRef();
		return S_OK;
	}

	*ppvObject = NULL;
	return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE ATUIDragDropHandler::DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
	mDropEffect = DROPEFFECT_NONE;

	if (!(GetWindowLong(mhwnd, GWL_STYLE) & WS_DISABLED)) {
		FORMATETC etc;
		etc.cfFormat = CF_HDROP;
		etc.dwAspect = DVASPECT_CONTENT;
		etc.lindex = -1;
		etc.ptd = NULL;
		etc.tymed = TYMED_HGLOBAL;

		HRESULT hr = pDataObj->QueryGetData(&etc);

		// Note that we get S_FALSE if the format is not supported.
		if (hr == S_OK) {
			SetDropEffect(grfKeyState);
		} else {
			etc.cfFormat = mClipFormatFileDescriptorA;
			etc.lindex = -1;
			etc.tymed = TYMED_HGLOBAL;

			hr = pDataObj->QueryGetData(&etc);

			if (hr != S_OK) {
				etc.cfFormat = mClipFormatFileDescriptorW;

				hr = pDataObj->QueryGetData(&etc);
			}

			if (hr == S_OK) {
				etc.lindex = 0;
				etc.cfFormat = mClipFormatFileContents;

				hr = pDataObj->QueryGetData(&etc);

				if (hr == S_OK) {
					SetDropEffect(grfKeyState);
				}
			}
		}
	}

	*pdwEffect = mDropEffect;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDragDropHandler::DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
	if (mDropEffect != DROPEFFECT_NONE) {
		SetDropEffect(grfKeyState);
	}

	*pdwEffect = mDropEffect;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDragDropHandler::DragLeave() {
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDragDropHandler::Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
	if (GetWindowLong(mhwnd, GWL_STYLE) & WS_DISABLED)
		return S_OK;

	FORMATETC etc;
	etc.cfFormat = CF_HDROP;
	etc.dwAspect = DVASPECT_CONTENT;
	etc.lindex = -1;
	etc.ptd = NULL;
	etc.tymed = TYMED_HGLOBAL;

	STGMEDIUM medium;
	medium.tymed = TYMED_HGLOBAL;
	medium.hGlobal = NULL;
	medium.pUnkForRelease = NULL;
	HRESULT hr = pDataObj->GetData(&etc, &medium);

	bool coldBoot = !(grfKeyState & MK_SHIFT);
	int loadIndex = -1;
	ATLoadType loadType = kATLoadType_Other;

	if (hr == S_OK) {
		if (mbOpenContextMenu) {
			if (!GetLoadTarget(pt, loadType, loadIndex, coldBoot))
				goto aborted;
		}

		HDROP hdrop = (HDROP)medium.hGlobal;

		UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0);

		if (count) {
			UINT len = DragQueryFileW(hdrop, 0, NULL, 0);

			vdfastvector<wchar_t> buf(len+1, 0);
			if (DragQueryFileW(hdrop, 0, buf.data(), len+1)) {

				try {
					if (coldBoot)
						DoBootWithConfirm(buf.data(), false, false, 0);
					else
						DoLoad((VDGUIHandle)g_hwnd, buf.data(), false, false, 0, loadType, NULL, loadIndex);
				} catch(const MyError& e) {
					e.post(g_hwnd, "Altirra Error");
				}
			}
		}
	} else {
		// Okay, that can't get an HDROP. Let's see if we can get a file contents stream.
		etc.cfFormat = mClipFormatFileDescriptorW;
		etc.dwAspect = DVASPECT_CONTENT;
		etc.lindex = -1;
		etc.ptd = NULL;
		etc.tymed = TYMED_HGLOBAL;

		medium.tymed = TYMED_HGLOBAL;
		medium.hGlobal = NULL;
		medium.pUnkForRelease = NULL;

		hr = pDataObj->GetData(&etc, &medium);

		bool foundFile = false;
		FILEDESCRIPTORW fd;

		if (hr == S_OK) {
			HGLOBAL hgDesc = (HGLOBAL)medium.hGlobal;

			FILEGROUPDESCRIPTORW *groupDesc = (FILEGROUPDESCRIPTORW *)GlobalLock(hgDesc);
			if (groupDesc) {
				if (groupDesc->cItems) {
					fd = groupDesc->fgd[0];
					foundFile = true;
				}

				GlobalUnlock(hgDesc);
			}

			ReleaseStgMedium(&medium);
		} else {
			etc.cfFormat = mClipFormatFileDescriptorA;

			hr = pDataObj->GetData(&etc, &medium);

			if (hr == S_OK) {
				HGLOBAL hgDesc = (HGLOBAL)medium.hGlobal;

				FILEGROUPDESCRIPTORA *groupDesc = (FILEGROUPDESCRIPTORA *)GlobalLock(hgDesc);
				if (groupDesc) {
					if (groupDesc->cItems) {
						const FILEDESCRIPTORA& fda = groupDesc->fgd[0];

						fd.dwFlags = fda.dwFlags;
						fd.clsid = fda.clsid;
						fd.sizel = fda.sizel;
						fd.pointl = fda.pointl;
						fd.dwFileAttributes = fda.dwFileAttributes;
						fd.ftCreationTime = fda.ftCreationTime;
						fd.ftLastAccessTime = fda.ftLastAccessTime;
						fd.ftLastWriteTime = fda.ftLastWriteTime;
						fd.nFileSizeHigh = fda.nFileSizeHigh;
						fd.nFileSizeLow = fda.nFileSizeLow;

						fd.cFileName[0] = 0;
						VDTextAToW(fd.cFileName, vdcountof(fd.cFileName), fda.cFileName);
						fd.cFileName[vdcountof(fd.cFileName) - 1] = 0;

						foundFile = true;
					}

					GlobalUnlock(hgDesc);
				}

				ReleaseStgMedium(&medium);
			}
		}

		medium.hGlobal = NULL;
		medium.pUnkForRelease = NULL;

		if (foundFile) {
			try {
				uint32 knownSize = 0;

				if (fd.dwFlags & FD_FILESIZE) {
					if (fd.nFileSizeHigh || fd.nFileSizeLow > 64*1024*1024)
						throw MyError("The dragged file is too large to load (>64MB).");

					knownSize = fd.nFileSizeLow;
				}

				etc.cfFormat = mClipFormatFileContents;
				etc.dwAspect = DVASPECT_CONTENT;
				etc.lindex = 0;
				etc.ptd = NULL;
				etc.tymed = TYMED_ISTREAM | TYMED_HGLOBAL;

				medium.tymed = TYMED_ISTREAM;

				hr = pDataObj->GetData(&etc, &medium);

				if (hr == S_OK) {
					if (mbOpenContextMenu) {
						if (!GetLoadTarget(pt, loadType, loadIndex, coldBoot))
							goto aborted;
					}

					vdfastvector<char> data;

					data.reserve(knownSize);

					if (medium.tymed == TYMED_ISTREAM)
						ATReadCOMStreamIntoMemory(data, medium.pstm);
					else if (medium.tymed == TYMED_HGLOBAL)
						ATReadCOMBufferIntoMemory(data, medium.hGlobal, knownSize);

					if (coldBoot)
						DoBootStreamWithConfirm(fd.cFileName, VDMemoryStream(data.data(), data.size()), false, 0);
					else
						DoLoadStream((VDGUIHandle)g_hwnd, fd.cFileName, VDMemoryStream(data.data(), data.size()), false, 0, loadType, NULL, loadIndex);
				}

			} catch(const MyError& e) {
				e.post(g_hwnd, "Altirra Error");
			}
		}
	}

aborted:
	ReleaseStgMedium(&medium);

	return S_OK;
}

bool ATUIDragDropHandler::GetLoadTarget(POINTL pt, ATLoadType& loadType, int& loadIndex, bool& doBoot) const {
	doBoot = false;
	loadIndex = -1;
	loadType = kATLoadType_Other;

	HMENU hmenu = LoadMenu(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDR_DRAGDROP_MENU));
	if (!hmenu)
		return false;

	HMENU hmenu2 = GetSubMenu(hmenu, 0);
	if (!hmenu2) {
		DestroyMenu(hmenu);
		return false;
	}

	static const UINT kDiskImageIds[]={
		ID_MOUNTIMAGE_D1,
		ID_MOUNTIMAGE_D2,
		ID_MOUNTIMAGE_D3,
		ID_MOUNTIMAGE_D4,
	};

	for(size_t i=0; i<vdcountof(kDiskImageIds); ++i) {
		ATDiskEmulator& disk = g_sim.GetDiskDrive(i);

		const wchar_t *path = disk.GetPath();

		VDStringW s = VDGetMenuItemTextByCommandW32(hmenu2, kDiskImageIds[i]);

		if (path)
			s.append_sprintf(L" [%ls]", path);
		else
			s += L" (empty)";

		VDSetMenuItemTextByCommandW32(hmenu2, kDiskImageIds[i], s.c_str());
	}

	UINT id = TrackPopupMenu(hmenu2, TPM_RETURNCMD, pt.x, pt.y, 0, g_hwnd, NULL);

	DestroyMenu(hmenu);

	if (!id)
		return false;

	switch(id) {
		case ID_DRAGDROP_BOOTIMAGE:
			doBoot = true;
			break;

		case ID_MOUNTIMAGE_D1:
			loadType = kATLoadType_Disk;
			loadIndex = 0;
			break;

		case ID_MOUNTIMAGE_D2:
			loadType = kATLoadType_Disk;
			loadIndex = 1;
			break;

		case ID_MOUNTIMAGE_D3:
			loadType = kATLoadType_Disk;
			loadIndex = 2;
			break;

		case ID_MOUNTIMAGE_D4:
			loadType = kATLoadType_Disk;
			loadIndex = 3;
			break;

		case ID_MOUNTIMAGE_CARTRIDGE:
			loadType = kATLoadType_Cartridge;
			loadIndex = 0;
			break;
	}

	return true;
}

void ATUIDragDropHandler::SetDropEffect(DWORD grfKeyState) {
	mDropEffect = DROPEFFECT_NONE;
	mbOpenContextMenu = false;

	if (grfKeyState & MK_RBUTTON) {
		mDropEffect = DROPEFFECT_COPY;
		mbOpenContextMenu = true;
	} else if (grfKeyState & MK_SHIFT)
		mDropEffect = DROPEFFECT_COPY;
	else
		mDropEffect = DROPEFFECT_MOVE;
}

///////////////////////////////////////////////////////////////////////////

void ATUIRegisterDragDropHandler(VDGUIHandle h) {
	RegisterDragDrop((HWND)h, vdrefptr<IDropTarget>(new ATUIDragDropHandler((HWND)h)));
}

void ATUIRevokeDragDropHandler(VDGUIHandle h) {
	RevokeDragDrop((HWND)h);
}

