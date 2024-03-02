//	Altirra - Atari 800/800XL/5200 emulator
//	Native UI library - drag and drop support
//	Copyright (C) 2009-2012 Avery Lee
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
#include <shlwapi.h>
#include <vd2/system/refcount.h>
#include <vd2/system/strutil.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include <at/atcore/vfs.h>
#include <at/atnativeui/dragdrop.h>

const ATUIDragDropFormatsW32& ATUIInitDragDropFormatsW32() {
	static const ATUIDragDropFormatsW32 sFormats = [] {
		return ATUIDragDropFormatsW32 {
			RegisterClipboardFormat(CFSTR_FILECONTENTS),
			RegisterClipboardFormat(CFSTR_FILEDESCRIPTORA),
			RegisterClipboardFormat(CFSTR_FILEDESCRIPTORW),
			RegisterClipboardFormat(CFSTR_SHELLIDLIST),
		};
	} ();

	return sFormats;
}

void ATAutoStgMediumW32::Clear() {
	if (tymed != TYMED_NULL) {
		ReleaseStgMedium(this);

		tymed = TYMED_NULL;
		pUnkForRelease = nullptr;
	}
}

void ATReadDragDropFileDescriptorsW32(vdfastvector<FILEDESCRIPTORW>& dst, const FILEGROUPDESCRIPTORW *src) {
	if (src->cItems)
		dst.assign(src->fgd, src->fgd + src->cItems);
	else
		dst.clear();
}
	
void ATReadDragDropFileDescriptorsW32(vdfastvector<FILEDESCRIPTORW>& dst, const FILEGROUPDESCRIPTORA *src) {
	dst.resize(src->cItems);

	for(UINT i=0; i<src->cItems; ++i) {
		FILEDESCRIPTORW& filew = dst[i];
		const FILEDESCRIPTORA& filea = src->fgd[i];

		filew.dwFlags			= filea.dwFlags;
		filew.clsid				= filea.clsid;
		filew.sizel				= filea.sizel;
		filew.pointl			= filea.pointl;
		filew.dwFileAttributes	= filea.dwFileAttributes;
		filew.ftCreationTime	= filea.ftCreationTime;
		filew.ftLastAccessTime	= filea.ftLastAccessTime;
		filew.ftLastWriteTime	= filea.ftLastWriteTime;
		filew.nFileSizeHigh		= filea.nFileSizeHigh;
		filew.nFileSizeLow		= filea.nFileSizeLow;

		VDTextAToW(filew.cFileName, vdcountof(filew.cFileName), filea.cFileName, -1);
	}
}

bool ATUIGetVFSPathFromShellIDListW32(HGLOBAL hGlobal, VDStringW& vfsPath) {
	CIDA *c = (CIDA *)GlobalLock(hGlobal);
	if (!c)
		return false;

	// check for exactly one IDList
	bool success = false;
	if (c->cidl == 1) {
		// pull out the parent PIDL and child item ID
		const PCUIDLIST_ABSOLUTE parentIDList = (PCUIDLIST_ABSOLUTE)((char *)c + c->aoffset[0]);
		const PCUITEMID_CHILD childID = (PCUITEMID_CHILD)((char *)c + c->aoffset[1]);

		// bind to the child and see if it's filesystem based
		vdrefptr<IShellFolder> desktop;
		HRESULT hr = SHGetDesktopFolder(~desktop);
		if (SUCCEEDED(hr)) {
			vdrefptr<IShellFolder> parent;
			hr = desktop->BindToObject(parentIDList, NULL, IID_IShellFolder, (void **)~parent);
			if (SUCCEEDED(hr)) {
				// Check if the child is not filesystem based.
				SFGAOF flags = SFGAO_FILESYSTEM;
				hr = parent->GetAttributesOf(1, &childID, &flags);
				if (SUCCEEDED(hr)) {
					if (!(flags & SFGAO_FILESYSTEM)) {
						// Okay, the child is not filesystem based. Walk the parent chain and try to find the
						// last item that is filesystem based.
						vdrefptr<IShellFolder> prev = desktop;
						VDStringW lastValidPath;
						VDStringW relPath;

						for(PCUIDLIST_RELATIVE relIDList = parentIDList; relIDList; relIDList = ILGetNext(relIDList)) {
							vdblock<char> buf(relIDList->mkid.cb + 2);

							memset(buf.data(), 0, relIDList->mkid.cb + 2);
							memcpy(buf.data(), &relIDList->mkid, relIDList->mkid.cb);

							vdrefptr<IShellFolder> next;
							hr = prev->BindToObject((PCUIDLIST_RELATIVE)buf.data(), nullptr, IID_IShellFolder, (void **)~next);
							if (FAILED(hr))
								break;

							// check if the next folder is filesystem based but its children are not
							PCUITEMID_CHILD queryPtr = (PCUITEMID_CHILD)buf.data();
							SFGAOF flags = SFGAO_FILESYSTEM | SFGAO_FILESYSANCESTOR;
							hr = prev->GetAttributesOf(1, &queryPtr, &flags);
							if (SUCCEEDED(hr) && (flags & SFGAO_FILESYSTEM) && !(flags & SFGAO_FILESYSANCESTOR)) {
								lastValidPath.clear();
								relPath.clear();

								STRRET sr = {};
								sr.uType = STRRET_WSTR;
								hr = prev->GetDisplayNameOf((PCUITEMID_CHILD)buf.data(), SHGDN_FORPARSING, &sr);
								if (SUCCEEDED(hr)) {
									LPWSTR s;
									hr = StrRetToStrW(&sr, nullptr, &s);
									if (SUCCEEDED(hr)) {
										lastValidPath = s;
										CoTaskMemFree(s);
									}
								}
							} else {
								STRRET sr = {};
								sr.uType = STRRET_WSTR;
								hr = prev->GetDisplayNameOf((PCUITEMID_CHILD)buf.data(), SHGDN_FORPARSING | SHGDN_INFOLDER, &sr);
								if (SUCCEEDED(hr)) {
									LPWSTR s;
									hr = StrRetToStrW(&sr, nullptr, &s);
									if (SUCCEEDED(hr)) {
										if (!relPath.empty())
											relPath += L'\\';

										relPath += s;
										CoTaskMemFree(s);
									}
								}
							}

							prev = std::move(next);
						}

						if (lastValidPath.size() > 4 && !vdwcsicmp(lastValidPath.c_str() + lastValidPath.size() - 4, L".zip")) {
							// Okay, we got a plausible .zip file. Get the parsing name for the child component and try
							// to open the path through VFS.

							STRRET sr = {};
							sr.uType = STRRET_WSTR;
							hr = prev->GetDisplayNameOf(childID, SHGDN_FORPARSING | SHGDN_INFOLDER, &sr);
							if (SUCCEEDED(hr)) {
								LPWSTR s;
								hr = StrRetToStrW(&sr, nullptr, &s);
								if (SUCCEEDED(hr)) {
									if (!relPath.empty())
										relPath += '\\';

									relPath += s;
									CoTaskMemFree(s);

									vfsPath = L"zip://";
									ATEncodeVFSPath(vfsPath, lastValidPath, true);
									vfsPath += L'!';
									ATEncodeVFSPath(vfsPath, relPath, true);

									success = true;
								}
							}
						}
					}
				}
			}
		}
	}

	GlobalUnlock(c);

	return success;
}

///////////////////////////////////////////////////////////////////////////

ULONG STDMETHODCALLTYPE ATUIDropTargetBaseW32::AddRef() {
	return ++mRefCount;
}

ULONG STDMETHODCALLTYPE ATUIDropTargetBaseW32::Release() {
	DWORD rc = --mRefCount;

	if (!rc)
		delete this;

	return rc;
}

HRESULT STDMETHODCALLTYPE ATUIDropTargetBaseW32::QueryInterface(REFIID riid, void **ppvObj) {
	if (riid == IID_IDropTarget)
		*ppvObj = static_cast<IDropTarget *>(this);
	else if (riid == IID_IUnknown)
		*ppvObj = static_cast<IUnknown *>(this);
	else {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	AddRef();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDropTargetBaseW32::DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
	OnDragOver(pt.x, pt.y);

	*pdwEffect = mDropEffect;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDropTargetBaseW32::DragLeave() {
	OnDragLeave();
	return S_OK;
}

void ATUIDropTargetBaseW32::OnDragOver(sint32 x, sint32 y) {
}

void ATUIDropTargetBaseW32::OnDragLeave() {
}
