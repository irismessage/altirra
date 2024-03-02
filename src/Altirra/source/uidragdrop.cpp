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
#include <vd2/system/atomic.h>
#include <vd2/system/error.h>
#include <vd2/system/vdstl.h>
#include "simulator.h"

extern HWND g_hwnd;
extern ATSimulator g_sim;

extern void DoLoad(VDGUIHandle h, const wchar_t *path, bool vrw, bool rw, int cartmapper);

///////////////////////////////////////////////////////////////////////////////

class ATUIDragDropHandler : public IDropTarget {
public:
	ATUIDragDropHandler();

	virtual ULONG STDMETHODCALLTYPE AddRef();
	virtual ULONG STDMETHODCALLTYPE Release();
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(const IID& riid, void **ppvObject);
    virtual HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);
    virtual HRESULT STDMETHODCALLTYPE DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);
    virtual HRESULT STDMETHODCALLTYPE DragLeave();
    virtual HRESULT STDMETHODCALLTYPE Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);

protected:
	VDAtomicInt mRefCount;
	DWORD mDropEffect;
};

///////////////////////////////////////////////////////////////////////////////

ATUIDragDropHandler::ATUIDragDropHandler()
	: mRefCount(0)
	, mDropEffect(DROPEFFECT_NONE)
{
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

	FORMATETC etc;
	etc.cfFormat = CF_HDROP;
	etc.dwAspect = DVASPECT_CONTENT;
	etc.lindex = -1;
	etc.ptd = NULL;
	etc.tymed = TYMED_HGLOBAL;

	HRESULT hr = pDataObj->QueryGetData(&etc);

	if (SUCCEEDED(hr)) {
		if (grfKeyState & MK_SHIFT)
			mDropEffect = DROPEFFECT_COPY;
		else
			mDropEffect = DROPEFFECT_MOVE;
	}

	*pdwEffect = mDropEffect;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDragDropHandler::DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
	if (mDropEffect != DROPEFFECT_NONE) {
		if (grfKeyState & MK_SHIFT)
			mDropEffect = DROPEFFECT_COPY;
		else
			mDropEffect = DROPEFFECT_MOVE;
	}

	*pdwEffect = mDropEffect;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDragDropHandler::DragLeave() {
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDragDropHandler::Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
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

	if (SUCCEEDED(hr)) {
		HDROP hdrop = (HDROP)medium.hGlobal;

		UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0);

		if (count) {
			UINT len = DragQueryFileW(hdrop, 0, NULL, 0);

			vdfastvector<wchar_t> buf(len+1, 0);
			if (DragQueryFileW(hdrop, 0, buf.data(), len+1)) {
				try {
					bool coldBoot = !(grfKeyState & MK_SHIFT);
					if (coldBoot)
						g_sim.UnloadAll();

					DoLoad((VDGUIHandle)g_hwnd, buf.data(), false, false, 0);

					if (coldBoot) {
						g_sim.ColdReset();
						g_sim.Resume();
					}
				} catch(const MyError& e) {
					e.post(g_hwnd, "Altirra Error");
				}
			}
		}
	}

	ReleaseStgMedium(&medium);
	return S_OK;
}

void ATUIRegisterDragDropHandler(VDGUIHandle h) {
	RegisterDragDrop((HWND)h, vdrefptr<IDropTarget>(new ATUIDragDropHandler));
}

void ATUIRevokeDragDropHandler(VDGUIHandle h) {
	RevokeDragDrop((HWND)h);
}

