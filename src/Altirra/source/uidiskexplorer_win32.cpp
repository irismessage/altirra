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
#include <windows.h>
#include <shellapi.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/strutil.h>
#include <at/atnativeui/dragdrop.h>
#include <at/atio/diskfs.h>
#include "uidiskexplorer_win32.h"

template<class... Interfaces>
class ATCOMBaseW32 : public Interfaces... {
public:
	virtual ~ATCOMBaseW32() = default;

	ULONG STDMETHODCALLTYPE AddRef() override final;
	ULONG STDMETHODCALLTYPE Release() override final;

protected:
	VDAtomicInt mRefCount{0};
};

template<class... Interfaces>
ULONG STDMETHODCALLTYPE ATCOMBaseW32<Interfaces...>::AddRef() {
	return ++mRefCount;
}

template<class... Interfaces>
ULONG STDMETHODCALLTYPE ATCOMBaseW32<Interfaces...>::Release() {
	DWORD rc = --mRefCount;

	if (!rc)
		delete this;

	return rc;
}

template<class Base, class... Interfaces>
class ATCOMQIW32 : public Base {
public:
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
};

namespace {
	static_assert(std::is_assignable_v<IUnknown *&, IDataObject *>);

	template<typename T, typename U>
	T ATStaticCastFirst(U ptr) {
		return nullptr;
	}

	template<typename T, typename U> requires std::is_assignable_v<T&, U>
	T ATStaticCastFirst(U ptr) {
		return static_cast<T>(ptr);
	}

	template<typename T, typename... Interfaces>
	T ATStaticCastFirst(ATCOMBaseW32<Interfaces...> *ptr) {
		T result = nullptr;

		(void)(... || (result = ATStaticCastFirst<T>(static_cast<Interfaces *>(ptr))));

		return result;
	}

	template<typename T, typename Base, typename... Interfaces>
	T ATStaticCastFirst(ATCOMQIW32<Base, Interfaces...> *ptr) {
		return ATStaticCastFirst<T>(static_cast<Base *>(ptr));
	}
}

template<typename Base, class... Interfaces>
HRESULT STDMETHODCALLTYPE ATCOMQIW32<Base, Interfaces...>::QueryInterface(REFIID riid, void **ppvObj) {
	if ((... || (riid == __uuidof(Interfaces) ? (*ppvObj = ATStaticCastFirst<Interfaces *>(this)), true : false))) {
		this->AddRef();
		return S_OK;
	}

	*ppvObj = nullptr;
	return E_NOINTERFACE;
}

class ATUIDiskExplorerGenericDropSource final : public ATCOMQIW32<ATCOMBaseW32<IDropSource>, IDropSource, IUnknown> {
public:
	HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override;
	HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD dwEffect) override;
};

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerGenericDropSource::QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) {
	if (fEscapePressed)
		return DRAGDROP_S_CANCEL;

	if (!(grfKeyState & (MK_LBUTTON | MK_RBUTTON)))
		return DRAGDROP_S_DROP;

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerGenericDropSource::GiveFeedback(DWORD dwEffect) {
	return DRAGDROP_S_USEDEFAULTCURSORS;
}

class ATUIDiskExplorerDataObjectFormatEnumerator : public ATCOMQIW32<ATCOMBaseW32<IEnumFORMATETC>, IEnumFORMATETC, IUnknown> {
public:
	ATUIDiskExplorerDataObjectFormatEnumerator();

    HRESULT STDMETHODCALLTYPE Next(ULONG celt, FORMATETC *rgelt, ULONG *pceltFetched);
    HRESULT STDMETHODCALLTYPE Skip(ULONG celt);
    HRESULT STDMETHODCALLTYPE Reset();
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC **ppenum);

private:
	ULONG mPos;
};

ATUIDiskExplorerDataObjectFormatEnumerator::ATUIDiskExplorerDataObjectFormatEnumerator()
	: mPos(0)
{
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDataObjectFormatEnumerator::Next(ULONG celt, FORMATETC *rgelt, ULONG *pceltFetched) {
	const auto& formats = ATUIInitDragDropFormatsW32();

	ULONG fetched = 0;

	memset(rgelt, 0, sizeof(rgelt[0])*celt);

	while(celt && mPos < 3) {
		switch(mPos) {
			case 0:
				rgelt->cfFormat = formats.mDescriptorA;
				rgelt->dwAspect = DVASPECT_CONTENT;
				rgelt->lindex = -1;
				rgelt->ptd = NULL;
				rgelt->tymed = TYMED_HGLOBAL;
				break;

			case 1:
				rgelt->cfFormat = formats.mDescriptorW;
				rgelt->dwAspect = DVASPECT_CONTENT;
				rgelt->lindex = -1;
				rgelt->ptd = NULL;
				rgelt->tymed = TYMED_HGLOBAL;
				break;

			default:
				// confirmed against shell data object: we should not report this per lindex (file)
				rgelt->cfFormat = formats.mContents;
				rgelt->dwAspect = DVASPECT_CONTENT;
				rgelt->lindex = -1;
				rgelt->ptd = NULL;
				rgelt->tymed = TYMED_ISTREAM;
				break;
		}

		++rgelt;
		--celt;
		++mPos;
		++fetched;
	}

	if (pceltFetched)
		*pceltFetched = fetched;

	return celt ? S_FALSE : S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDataObjectFormatEnumerator::Skip(ULONG celt) {
	if (!celt)
		return S_OK;

	if (mPos >= 3 || 3 - mPos < celt) {
		mPos = 3;
		return S_FALSE;
	}

	mPos += celt;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDataObjectFormatEnumerator::Reset() {
	mPos = 0;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDataObjectFormatEnumerator::Clone(IEnumFORMATETC **ppenum) {
	ATUIDiskExplorerDataObjectFormatEnumerator *clone = new_nothrow ATUIDiskExplorerDataObjectFormatEnumerator;
	*ppenum = clone;

	if (!clone)
		return E_OUTOFMEMORY;

	clone->mPos = mPos;
	clone->AddRef();
	return S_OK;
}

class ATUIDiskExplorerDataObject : public ATCOMQIW32<ATCOMBaseW32<IDataObject, IATUIDiskExplorerDataObjectW32>, IDataObject, IUnknown> {
	ATUIDiskExplorerDataObject(const ATUIDiskExplorerDataObject&) = delete;
	ATUIDiskExplorerDataObject& operator=(const ATUIDiskExplorerDataObject&) = delete;
public:
	ATUIDiskExplorerDataObject(IATDiskFS *fs);
	~ATUIDiskExplorerDataObject();

	IDataObject *AsDataObject() override { return this; }
	void AddFile(const ATDiskFSKey& fskey, uint32 bytes, const VDExpandedDate *date, const wchar_t *filename) override;

	HRESULT STDMETHODCALLTYPE GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium);
	HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *pformatetc, STGMEDIUM *pmedium);
	HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *pformatetc);
	HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *pformatectIn, FORMATETC *pformatetcOut);
	HRESULT STDMETHODCALLTYPE SetData(FORMATETC *pformatetc, STGMEDIUM *pmedium, BOOL fRelease);
	HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc);
	HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection);
	HRESULT STDMETHODCALLTYPE DUnadvise(DWORD dwConnection);
	HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **ppenumAdvise);

protected:
	void GenerateFileDescriptors(FILEGROUPDESCRIPTORA *group);
	void GenerateFileDescriptors(FILEGROUPDESCRIPTORW *group);

	IATDiskFS *mpFS = nullptr;

	struct FileEntry {
		ATDiskFSKey mFsKey {};
		uint32 mBytes = 0;
		bool mbDateValid = false;
		VDExpandedDate mDate {};
		VDStringW mFileName;
	};

	vdvector<FileEntry> mFiles;

	vdrefptr<IDataObject> mpShellDataObj;
};

void ATUICreateDiskExplorerDataObjectW32(IATDiskFS *fs, IATUIDiskExplorerDataObjectW32 **target) {
	ATUIDiskExplorerDataObject *p = new ATUIDiskExplorerDataObject(fs);
	p->AddRef();

	*target = p;
}

ATUIDiskExplorerDataObject::ATUIDiskExplorerDataObject(IATDiskFS *fs)
	: mpFS(fs)
{
}

ATUIDiskExplorerDataObject::~ATUIDiskExplorerDataObject() {
}

void ATUIDiskExplorerDataObject::AddFile(const ATDiskFSKey& fskey, uint32 bytes, const VDExpandedDate *date, const wchar_t *filename) {
	FileEntry fe;
	fe.mFsKey = fskey;
	fe.mBytes = bytes;

	if (date) {
		fe.mDate = *date;
		fe.mbDateValid = true;
	}

	fe.mFileName = filename;
	mFiles.emplace_back(std::move(fe));
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDataObject::GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) {
	HRESULT hr = QueryGetData(pformatetcIn);

	if (FAILED(hr))
		return hr;

	const auto& formats = ATUIInitDragDropFormatsW32();

	if (pformatetcIn->cfFormat == formats.mDescriptorA) {
		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(FILEGROUPDESCRIPTORA) + sizeof(FILEDESCRIPTORA) * (mFiles.size() - 1));

		if (!hMem)
			return STG_E_MEDIUMFULL;

		void *p = GlobalLock(hMem);
		if (!p) {
			GlobalFree(hMem);
			return STG_E_MEDIUMFULL;
		}

		GenerateFileDescriptors((FILEGROUPDESCRIPTORA *)p);

		GlobalUnlock(hMem);

		pmedium->tymed = TYMED_HGLOBAL;
		pmedium->hGlobal = hMem;
		pmedium->pUnkForRelease = NULL;
	} else if (pformatetcIn->cfFormat == formats.mDescriptorW) {
		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(FILEGROUPDESCRIPTORW) + sizeof(FILEDESCRIPTORW) * (mFiles.size() - 1));

		if (!hMem)
			return STG_E_MEDIUMFULL;

		void *p = GlobalLock(hMem);
		if (!p) {
			GlobalFree(hMem);
			return STG_E_MEDIUMFULL;
		}

		GenerateFileDescriptors((FILEGROUPDESCRIPTORW *)p);

		GlobalUnlock(hMem);

		pmedium->tymed = TYMED_HGLOBAL;
		pmedium->hGlobal = hMem;
		pmedium->pUnkForRelease = NULL;
	} else if (pformatetcIn->cfFormat == formats.mContents) {
		vdrefptr<IStream> stream;
		hr = CreateStreamOnHGlobal(NULL, TRUE, ~stream);
		if (FAILED(hr))
			return STG_E_MEDIUMFULL;

		const FileEntry& fle = mFiles[pformatetcIn->lindex];

		vdfastvector<uint8> buf;

		pmedium->tymed = TYMED_ISTREAM;
		pmedium->pstm = NULL;
		pmedium->pUnkForRelease = NULL;

		try {
			mpFS->ReadFile(fle.mFsKey, buf);
		} catch(const ATDiskFSException& ex) {
			switch(ex.GetErrorCode()) {
				case kATDiskFSError_CorruptedFileSystem:
					return HRESULT_FROM_WIN32(ERROR_DISK_CORRUPT);

				case kATDiskFSError_DecompressionError:
					return HRESULT_FROM_WIN32(ERROR_FILE_CORRUPT);

				case kATDiskFSError_CRCError:
					return HRESULT_FROM_WIN32(ERROR_CRC);
			}

			return STG_E_READFAULT;
		} catch(const MyError&) {
			return STG_E_READFAULT;
		}

		stream->Write(buf.data(), (ULONG)buf.size(), NULL);

		// Required for Directory Opus to work.
		LARGE_INTEGER lizero = {0};
		stream->Seek(lizero, STREAM_SEEK_SET, NULL);

		pmedium->pstm = stream.release();
	} else {
		// if we have a shell data object, delegate to it
		if (mpShellDataObj)
			return mpShellDataObj->GetData(pformatetcIn, pmedium);

		return DV_E_FORMATETC;
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDataObject::GetDataHere(FORMATETC *pformatetc, STGMEDIUM *pmedium) {
	HRESULT hr = QueryGetData(pformatetc);

	if (FAILED(hr))
		return hr;
		
	const auto& formats = ATUIInitDragDropFormatsW32();

	if (pformatetc->cfFormat == formats.mDescriptorA) {
		SIZE_T bytesNeeded = sizeof(FILEGROUPDESCRIPTORA) + sizeof(FILEDESCRIPTORA)*(mFiles.size() - 1);

		if (pmedium->tymed != TYMED_HGLOBAL)
			return DV_E_TYMED;

		if (GlobalSize(pmedium->hGlobal) < bytesNeeded)
			return STG_E_MEDIUMFULL;

		void *p = GlobalLock(pmedium->hGlobal);
		if (!p)
			return STG_E_MEDIUMFULL;

		GenerateFileDescriptors((FILEGROUPDESCRIPTORA *)p);

		GlobalUnlock(pmedium->hGlobal);
	} else if (pformatetc->cfFormat == formats.mDescriptorW) {
		SIZE_T bytesNeeded = sizeof(FILEGROUPDESCRIPTORW) + sizeof(FILEDESCRIPTORW)*(mFiles.size() - 1);

		if (pmedium->tymed != TYMED_HGLOBAL)
			return DV_E_TYMED;

		if (GlobalSize(pmedium->hGlobal) < bytesNeeded)
			return STG_E_MEDIUMFULL;

		void *p = GlobalLock(pmedium->hGlobal);
		if (!p)
			return STG_E_MEDIUMFULL;

		GenerateFileDescriptors((FILEGROUPDESCRIPTORW *)p);

		GlobalUnlock(pmedium->hGlobal);
	} else if (pformatetc->cfFormat == formats.mContents) {
		vdrefptr<IStream> streamAlloc;
		IStream *stream;

		const FileEntry& fle = mFiles[pformatetc->lindex];

		if (pmedium->tymed == TYMED_HGLOBAL) {
			if (GlobalSize(pmedium->hGlobal) < fle.mBytes)
				return STG_E_MEDIUMFULL;

			hr = CreateStreamOnHGlobal(pmedium->hGlobal, FALSE, ~streamAlloc);
			if (FAILED(hr))
				return STG_E_MEDIUMFULL;

			stream = streamAlloc;
		} else if (pmedium->tymed == TYMED_ISTREAM) {
			stream = pmedium->pstm;
		} else
			return DV_E_TYMED;

		vdfastvector<uint8> buf;

		try {
			mpFS->ReadFile(fle.mFsKey, buf);
		} catch(const ATDiskFSException& ex) {
			switch(ex.GetErrorCode()) {
				case kATDiskFSError_CorruptedFileSystem:
					return HRESULT_FROM_WIN32(ERROR_DISK_CORRUPT);

				case kATDiskFSError_DecompressionError:
					return HRESULT_FROM_WIN32(ERROR_FILE_CORRUPT);

				case kATDiskFSError_CRCError:
					return HRESULT_FROM_WIN32(ERROR_CRC);
			}

			return STG_E_READFAULT;
		} catch(const MyError&) {
			return STG_E_READFAULT;
		}

		stream->Write(buf.data(), (ULONG)buf.size(), NULL);
	} else {
		// if we have a shell data object, delegate to it
		if (mpShellDataObj)
			return mpShellDataObj->GetDataHere(pformatetc, pmedium);

		return DV_E_FORMATETC;
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDataObject::QueryGetData(FORMATETC *pformatetc) {
	const auto& formats = ATUIInitDragDropFormatsW32();

	if (pformatetc->cfFormat == formats.mContents) {
		if ((uint32)pformatetc->lindex >= mFiles.size())
			return DV_E_LINDEX;

		if (!(pformatetc->tymed & (TYMED_ISTREAM | TYMED_HGLOBAL)))
			return DV_E_TYMED;
	} else if (pformatetc->cfFormat == formats.mDescriptorA || pformatetc->cfFormat == formats.mDescriptorW) {
		if (pformatetc->lindex != -1)
			return DV_E_LINDEX;

		if (!(pformatetc->tymed & TYMED_HGLOBAL))
			return DV_E_TYMED;
	} else {
		if (mpShellDataObj)
			return mpShellDataObj->QueryGetData(pformatetc);

		return DV_E_CLIPFORMAT;
	}

	if (pformatetc->dwAspect != DVASPECT_CONTENT)
		return DV_E_DVASPECT;

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDataObject::GetCanonicalFormatEtc(FORMATETC *pformatetcIn, FORMATETC *pformatetcOut) {
	*pformatetcOut = *pformatetcIn;
	pformatetcOut->ptd = NULL;

	return DATA_S_SAMEFORMATETC;
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDataObject::SetData(FORMATETC *pformatetc, STGMEDIUM *pmedium, BOOL fRelease) {
	// If we're on at least Windows Vista, create and delegate to a shell data object
	// so we can store data types needed for rich drag and drop reporting to work
	// (DROPDESCRIPTION and DragWindow).

	if (!mpShellDataObj) {
		HRESULT hr = SHCreateDataObject(nullptr, 0, nullptr, nullptr, IID_IDataObject, (void **)~mpShellDataObj);
		if (FAILED(hr))
			return hr;
	}

	return mpShellDataObj->SetData(pformatetc, pmedium, fRelease);
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDataObject::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc) {
	if (dwDirection != DATADIR_GET) {
		*ppenumFormatEtc = NULL;
		return E_NOTIMPL;
	}

	// The Windows SDK DragDropVisuals sample doesn't delegate to the shell object for
	// EnumFormatEtc()... hmm. Well, at least that makes our life easier.
	*ppenumFormatEtc = new_nothrow ATUIDiskExplorerDataObjectFormatEnumerator;
	if (!*ppenumFormatEtc)
		return E_OUTOFMEMORY;

	(*ppenumFormatEtc)->AddRef();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDataObject::DAdvise(FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection) {
	return OLE_E_ADVISENOTSUPPORTED;
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDataObject::DUnadvise(DWORD dwConnection) {
	return OLE_E_ADVISENOTSUPPORTED;
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDataObject::EnumDAdvise(IEnumSTATDATA **ppenumAdvise) {
	return OLE_E_ADVISENOTSUPPORTED;
}

void ATUIDiskExplorerDataObject::GenerateFileDescriptors(FILEGROUPDESCRIPTORA *group) {
	uint32 n = (uint32)mFiles.size();
	group->cItems = (DWORD)n;

	for(uint32 i=0; i<n; ++i) {
		const FileEntry& fle = mFiles[i];
		FILEDESCRIPTORA *fd = &group->fgd[i];

		memset(fd, 0, sizeof *fd);
			
		fd->dwFlags = FD_FILESIZE | FD_ATTRIBUTES;
		fd->nFileSizeLow = fle.mBytes;
		fd->nFileSizeHigh = 0;
		fd->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;

		if (fle.mbDateValid) {
			const VDDate creationDate = VDDateFromLocalDate(fle.mDate);

			fd->ftCreationTime.dwLowDateTime = (uint32)creationDate.mTicks;
			fd->ftCreationTime.dwHighDateTime = (uint32)(creationDate.mTicks >> 32);
			fd->ftLastWriteTime = fd->ftCreationTime;
			fd->dwFlags |= FD_CREATETIME | FD_WRITESTIME;
		}

		vdstrlcpy(fd->cFileName, VDTextWToA(fle.mFileName).c_str(), vdcountof(fd->cFileName));
	}
}

void ATUIDiskExplorerDataObject::GenerateFileDescriptors(FILEGROUPDESCRIPTORW *group) {
	uint32 n = (uint32)mFiles.size();
	group->cItems = (DWORD)n;

	for(uint32 i=0; i<n; ++i) {
		const FileEntry& fle = mFiles[i];
		FILEDESCRIPTORW *fd = &group->fgd[i];

		memset(fd, 0, sizeof *fd);
			
		fd->dwFlags = FD_FILESIZE | FD_ATTRIBUTES;
		fd->nFileSizeLow = fle.mBytes;
		fd->nFileSizeHigh = 0;
		fd->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;

		if (fle.mbDateValid) {
			const VDDate creationDate = VDDateFromLocalDate(fle.mDate);

			fd->ftCreationTime.dwLowDateTime = (uint32)creationDate.mTicks;
			fd->ftCreationTime.dwHighDateTime = (uint32)(creationDate.mTicks >> 32);
			fd->ftLastWriteTime = fd->ftCreationTime;
			fd->dwFlags |= FD_CREATETIME | FD_WRITESTIME;
		}

		vdwcslcpy(fd->cFileName, fle.mFileName.c_str(), vdcountof(fd->cFileName));
	}
}

class ATUIDiskExplorerDropTarget final : public ATUIDropTargetBaseW32, public IATUIDiskExplorerDropTargetW32 {
public:
	ATUIDiskExplorerDropTarget(HWND hwnd, IATDropTargetNotify *notify);
	
	ULONG STDMETHODCALLTYPE AddRef() override { return ATUIDropTargetBaseW32::AddRef(); }
	ULONG STDMETHODCALLTYPE Release() override { return ATUIDropTargetBaseW32::Release(); }
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override { return ATUIDropTargetBaseW32::QueryInterface(riid, ppv); }

	IDropTarget *AsDropTarget() override { return this; }

	void SetFS(IATDiskFS *fs) override { mpFS = fs; }

	HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;
	HRESULT STDMETHODCALLTYPE DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;
	HRESULT STDMETHODCALLTYPE Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;

private:
	void OnDragLeave() override;

	void WriteFromStorageMedium(STGMEDIUM *medium, const char *filename, uint32 len, const FILETIME& creationTime);

	HWND mhwnd = nullptr;

	IATDiskFS *mpFS = nullptr;
	IATDropTargetNotify *mpNotify = nullptr;
	vdrefptr<IDataObject> mpDataObject;
	vdrefptr<IDropTargetHelper> mpDropTargetHelper;
};

void ATUICreateDiskExplorerDropTargetW32(HWND hwnd, IATDropTargetNotify *notify, IATUIDiskExplorerDropTargetW32 **target) {
	ATUIDiskExplorerDropTarget *p = new ATUIDiskExplorerDropTarget(hwnd, notify);
	p->AddRef();

	*target = p;
}

ATUIDiskExplorerDropTarget::ATUIDiskExplorerDropTarget(HWND hwnd, IATDropTargetNotify *notify)
	: mhwnd(hwnd)
	, mpNotify(notify)
{
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDropTarget::DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
	mDropEffect = DROPEFFECT_NONE;

	if (!(GetWindowLong(mhwnd, GWL_STYLE) & WS_DISABLED) && mpFS && !mpFS->IsReadOnly()) {
		const auto& formats = ATUIInitDragDropFormatsW32();

		FORMATETC etc {};
		etc.cfFormat = CF_HDROP;
		etc.dwAspect = DVASPECT_CONTENT;
		etc.lindex = -1;
		etc.ptd = NULL;
		etc.tymed = TYMED_HGLOBAL;

		HRESULT hr = pDataObj->QueryGetData(&etc);

		if (hr != S_OK) {
			etc.cfFormat = formats.mDescriptorW;
			hr = pDataObj->QueryGetData(&etc);

			if (hr != S_OK) {
				etc.cfFormat = formats.mDescriptorA;
				hr = pDataObj->QueryGetData(&etc);
			}
		}

		if (hr == S_OK) {
			mDropEffect = DROPEFFECT_COPY;
			mpDataObject = pDataObj;

			CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC, IID_IDropTargetHelper, (void **)~mpDropTargetHelper);

			if (mpDropTargetHelper) {
				POINT pt2 = { pt.x, pt.y };
				mpDropTargetHelper->DragEnter(mhwnd, pDataObj, &pt2, mDropEffect);
			}
		}
	}

	*pdwEffect = mDropEffect;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDropTarget::DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
	if (mpDataObject)
		ATUISetDropDescriptionW32(mpDataObject, (DROPIMAGETYPE)mDropEffect, L"Copy to disk image", nullptr);

	*pdwEffect = mDropEffect;

	if (mpDropTargetHelper) {
		POINT pt2 = { pt.x, pt.y };
		mpDropTargetHelper->DragOver(&pt2, mDropEffect);
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDiskExplorerDropTarget::Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
	mpDataObject = nullptr;

	if (mpDropTargetHelper) {
		POINT pt2 = { pt.x, pt.y };
		mpDropTargetHelper->Drop(pDataObj, &pt2, mDropEffect);
		mpDropTargetHelper = nullptr;
	}

	if (GetWindowLong(mhwnd, GWL_STYLE) & WS_DISABLED)
		return S_OK;
		
	if (!mpFS || mpFS->IsReadOnly())
		return S_OK;

	const auto& formats = ATUIInitDragDropFormatsW32();

	// pull filenames
	vdautoptr2<FILEGROUPDESCRIPTOR> descriptors;

	FORMATETC etc;
	etc.cfFormat = formats.mDescriptorA;
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
		FILEGROUPDESCRIPTORA *descriptors = (FILEGROUPDESCRIPTORA *)GlobalLock(medium.hGlobal);

		if (descriptors) {
			// read out the files, one at a time
			for(uint32 i = 0; i < descriptors->cItems; ++i) {
				const FILEDESCRIPTORA& fd = descriptors->fgd[i];
				uint64 len64 = fd.nFileSizeLow + ((uint64)fd.nFileSizeHigh << 32);

				if (len64 > 0x4000000)
					continue;

				etc.cfFormat = formats.mContents;
				etc.dwAspect = DVASPECT_CONTENT;
				etc.lindex = i;
				etc.ptd = NULL;
				etc.tymed = TYMED_HGLOBAL | TYMED_ISTREAM;

				STGMEDIUM medium2;
				medium2.tymed = TYMED_HGLOBAL;
				medium2.hGlobal = NULL;
				medium2.pUnkForRelease = NULL;
				hr = pDataObj->GetData(&etc, &medium2);

				if (SUCCEEDED(hr)) {
					try {
						WriteFromStorageMedium(&medium2, fd.cFileName, (uint32)len64, fd.ftCreationTime);
					} catch(const MyError& e) {
						e.post(mhwnd, "Altirra Error");
						break;
					}

					ReleaseStgMedium(&medium2);
				}
			}

			GlobalUnlock(medium.hGlobal);
		}

		ReleaseStgMedium(&medium);
	} else {
		etc.cfFormat = formats.mDescriptorW;
		hr = pDataObj->GetData(&etc, &medium);

		if (SUCCEEDED(hr)) {
			FILEGROUPDESCRIPTORW *descriptors = (FILEGROUPDESCRIPTORW *)GlobalLock(medium.hGlobal);

			if (descriptors) {
				// read out the files, one at a time
				for(uint32 i = 0; i < descriptors->cItems; ++i) {
					const FILEDESCRIPTORW& fd = descriptors->fgd[i];
					uint64 len64 = fd.nFileSizeLow + ((uint64)fd.nFileSizeHigh << 32);

					if (len64 > 0x4000000)
						continue;

					etc.cfFormat = formats.mContents;
					etc.dwAspect = DVASPECT_CONTENT;
					etc.lindex = i;
					etc.ptd = NULL;
					etc.tymed = TYMED_HGLOBAL | TYMED_ISTREAM;

					STGMEDIUM medium2;
					medium2.tymed = TYMED_HGLOBAL;
					medium2.hGlobal = NULL;
					medium2.pUnkForRelease = NULL;
					hr = pDataObj->GetData(&etc, &medium2);

					if (SUCCEEDED(hr)) {
						try {
							WriteFromStorageMedium(&medium2, VDTextWToA(fd.cFileName).c_str(), (uint32)len64, fd.dwFlags & FD_CREATETIME ? fd.ftCreationTime : FILETIME {});
						} catch(const MyError& e) {
							e.post(mhwnd, "Altirra Error");
							break;
						}

						ReleaseStgMedium(&medium2);
					}
				}

				GlobalUnlock(medium.hGlobal);
			}

			ReleaseStgMedium(&medium);
		} else {
			etc.cfFormat = CF_HDROP;
			hr = pDataObj->GetData(&etc, &medium);

			if (FAILED(hr))
				return hr;

			HDROP hdrop = (HDROP)medium.hGlobal;

			UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0);

			vdfastvector<wchar_t> buf;
			for(UINT i = 0; i < count; ++i) {
				UINT len = DragQueryFileW(hdrop, i, NULL, 0);

				buf.clear();
				buf.resize(len+1, 0);

				if (DragQueryFileW(hdrop, i, buf.data(), len+1)) {
					VDFile f(buf.data());

					sint64 len = f.size();

					try {
						if (len < 0x400000) {
							uint32 len32 = (uint32)len;

							vdfastvector<uint8> databuf(len32);
							f.read(databuf.data(), len32);

							const wchar_t *fn = VDFileSplitPath(buf.data());
							const VDStringA fn8(VDTextWToA(fn));

							mpNotify->WriteFile(fn8.c_str(), databuf.data(), len32, f.getCreationTime());
						}
					} catch(const MyError& e) {
						e.post(mhwnd, "Altirra Error");
						break;
					}
				}
			}

			ReleaseStgMedium(&medium);
		}
	}

	mpNotify->OnFSModified();
	return S_OK;
}

void ATUIDiskExplorerDropTarget::OnDragLeave() {
	if (mpDropTargetHelper) {
		mpDropTargetHelper->DragLeave();
		mpDropTargetHelper = nullptr;
	}

	ATUISetDropDescriptionW32(mpDataObject, DROPIMAGE_INVALID, nullptr, nullptr);
		
	mpDataObject = nullptr;
}

void ATUIDiskExplorerDropTarget::WriteFromStorageMedium(STGMEDIUM *medium, const char *filename, uint32 len32, const FILETIME& creationTime) {
	vdrefptr<IStream> stream;
	if (medium->tymed == TYMED_HGLOBAL) {
		CreateStreamOnHGlobal(medium->hGlobal, FALSE, ~stream);
	} else {
		stream = medium->pstm;
	}

	if (!stream)
		return;

	vdfastvector<uint8> buf;

	LARGE_INTEGER dist;
	dist.QuadPart = 0;
	HRESULT hr = stream->Seek(dist, STREAM_SEEK_SET, NULL);

	if (SUCCEEDED(hr)) {
		buf.resize(len32);

		ULONG actual = 0;
		if (len32 > 0)
			hr = stream->Read(buf.data(), len32, &actual);
		else
			hr = S_OK;

		if (SUCCEEDED(hr)) {
			const uint64 ticks = creationTime.dwLowDateTime + ((uint64)creationTime.dwHighDateTime << 32);
			mpNotify->WriteFile(filename, buf.data(), actual, VDDate { ticks });
		}
	}

	stream.clear();
}
