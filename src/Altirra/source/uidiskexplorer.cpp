//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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
#include <shlobj.h>
#include <shellapi.h>
#include <ole2.h>
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/event.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/strutil.h>
#include <vd2/system/thunk.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/services.h>
#include "resource.h"
#include "diskimage.h"
#include <at/atui/dialog.h>
#include <at/atui/uiproxies.h>
#include "diskfs.h"

struct ATUIDiskExplorerFileEntry {
public:
	VDStringW mFileName;
	uintptr mFileKey;
	uint32 mSectors;
	uint32 mBytes;
	bool mbIsDirectory;
	bool mbDateValid;
	VDExpandedDate mDate;

	void InitFrom(const ATDiskFSEntryInfo& einfo) {
		mFileName = VDTextAToW(einfo.mFileName);
		mSectors = einfo.mSectors;
		mBytes = einfo.mBytes;
		mFileKey = einfo.mKey;
		mbIsDirectory = einfo.mbIsDirectory;
		mbDateValid = einfo.mbDateValid;
		mDate = einfo.mDate;
	}
};

namespace {
	UINT gCFContents;
	UINT gCFDescriptorA;
	UINT gCFDescriptorW;

	void InitFileDropClipboardFormats() {
		gCFContents = RegisterClipboardFormat(CFSTR_FILECONTENTS);
		gCFDescriptorA = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORA);
		gCFDescriptorW = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORW);
	}
}

namespace {
	class IATDropTargetNotify {
	public:
		virtual uint32 GetDropTargetParentKey() const = 0;
		virtual void OnFSModified() = 0;
	};

	class DropTarget : public IDropTarget {
	public:
		DropTarget(HWND hwnd, IATDropTargetNotify *notify);

		void SetFS(IATDiskFS *fs) { mpFS = fs; }

		ULONG STDMETHODCALLTYPE AddRef();
		ULONG STDMETHODCALLTYPE Release();
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj);

		HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);
		HRESULT STDMETHODCALLTYPE DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);
		HRESULT STDMETHODCALLTYPE DragLeave();
		HRESULT STDMETHODCALLTYPE Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect);

	protected:
		void WriteFromStorageMedium(STGMEDIUM *medium, const char *filename, uint32 len);

		VDAtomicInt mRefCount;
		DWORD mDropEffect;
		HWND mhwnd;

		IATDiskFS *mpFS;
		IATDropTargetNotify *mpNotify;
	};

	DropTarget::DropTarget(HWND hwnd, IATDropTargetNotify *notify)
		: mRefCount(0)
		, mDropEffect(DROPEFFECT_NONE)
		, mhwnd(hwnd)
		, mpFS(NULL)
		, mpNotify(notify)
	{
		InitFileDropClipboardFormats();
	}

	ULONG STDMETHODCALLTYPE DropTarget::AddRef() {
		return ++mRefCount;
	}

	ULONG STDMETHODCALLTYPE DropTarget::Release() {
		DWORD rc = --mRefCount;

		if (!rc)
			delete this;

		return rc;
	}

	HRESULT STDMETHODCALLTYPE DropTarget::QueryInterface(REFIID riid, void **ppvObj) {
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

	HRESULT STDMETHODCALLTYPE DropTarget::DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
		mDropEffect = DROPEFFECT_NONE;

		if (!(GetWindowLong(mhwnd, GWL_STYLE) & WS_DISABLED) && mpFS && !mpFS->IsReadOnly()) {
			FORMATETC etc;
			etc.cfFormat = CF_HDROP;
			etc.dwAspect = DVASPECT_CONTENT;
			etc.lindex = -1;
			etc.ptd = NULL;
			etc.tymed = TYMED_HGLOBAL;

			HRESULT hr = pDataObj->QueryGetData(&etc);

			if (hr != S_OK) {
				etc.cfFormat = gCFDescriptorW;
				hr = pDataObj->QueryGetData(&etc);

				if (hr != S_OK) {
					etc.cfFormat = gCFDescriptorA;
					hr = pDataObj->QueryGetData(&etc);
				}
			}

			if (hr == S_OK) {
				if (grfKeyState & MK_SHIFT)
					mDropEffect = DROPEFFECT_COPY;
				else
					mDropEffect = DROPEFFECT_MOVE;
			}
		}

		*pdwEffect = mDropEffect;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DropTarget::DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
		if (mDropEffect != DROPEFFECT_NONE) {
			if (grfKeyState & MK_SHIFT)
				mDropEffect = DROPEFFECT_COPY;
			else
				mDropEffect = DROPEFFECT_MOVE;
		}

		*pdwEffect = mDropEffect;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DropTarget::DragLeave() {
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DropTarget::Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
		if ((GetWindowLong(mhwnd, GWL_STYLE) & WS_DISABLED) || !mpFS || mpFS->IsReadOnly())
			return S_OK;

		// pull filenames
		vdautoptr2<FILEGROUPDESCRIPTOR> descriptors;

		FORMATETC etc;
		etc.cfFormat = gCFDescriptorA;
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
					uint64 len64 = descriptors->fgd[i].nFileSizeLow + ((uint64)descriptors->fgd[i].nFileSizeHigh << 32);

					if (len64 > 0x4000000)
						continue;

					etc.cfFormat = gCFContents;
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
							WriteFromStorageMedium(&medium2, descriptors->fgd[i].cFileName, (uint32)len64);
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
			etc.cfFormat = gCFDescriptorW;
			hr = pDataObj->GetData(&etc, &medium);

			if (SUCCEEDED(hr)) {
				FILEGROUPDESCRIPTORW *descriptors = (FILEGROUPDESCRIPTORW *)GlobalLock(medium.hGlobal);

				if (descriptors) {
					// read out the files, one at a time
					for(uint32 i = 0; i < descriptors->cItems; ++i) {
						uint64 len64 = descriptors->fgd[i].nFileSizeLow + ((uint64)descriptors->fgd[i].nFileSizeHigh << 32);

						if (len64 > 0x4000000)
							continue;

						etc.cfFormat = gCFContents;
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
								WriteFromStorageMedium(&medium2, VDTextWToA(descriptors->fgd[i].cFileName).c_str(), (uint32)len64);
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

								mpFS->WriteFile(mpNotify->GetDropTargetParentKey(), fn8.c_str(), databuf.data(), len32);
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

	void DropTarget::WriteFromStorageMedium(STGMEDIUM *medium, const char *filename, uint32 len32) {
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

			ULONG actual;
			hr = stream->Read(buf.data(), len32, &actual);

			if (SUCCEEDED(hr))
				mpFS->WriteFile(0, filename, buf.data(), actual);
		}

		stream.clear();
	}
}

class ATUIDialogDiskExplorer : public VDDialogFrameW32, public IATDropTargetNotify {
public:
	ATUIDialogDiskExplorer(IATDiskImage *image = NULL, const wchar_t *imageName = NULL, bool writeEnabled = true, bool autoFlush = true);
	~ATUIDialogDiskExplorer();

protected:
	bool OnLoaded();
	void OnDestroy();
	void OnSize();
	bool OnErase(VDZHDC hdc);
	bool OnCommand(uint32 id, uint32 extcode);

	void OnItemBeginDrag(VDUIProxyListView *sender, int item);
	void OnItemContextMenu(VDUIProxyListView *sender, const VDUIProxyListView::ContextMenuEvent& event);
	void OnItemLabelChanged(VDUIProxyListView *sender, VDUIProxyListView::LabelChangedEvent *event);
	void OnItemDoubleClick(VDUIProxyListView *sender, int item);

	virtual uint32 GetDropTargetParentKey() const { return mCurrentDirKey; }
	void OnFSModified();

	void RefreshList();

	LRESULT ListViewSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	class FileListEntry : public vdrefcounted<IVDUIListViewVirtualItem>, public ATUIDiskExplorerFileEntry {
	public:
		void GetText(int subItem, VDStringW& s) const;
	};

	struct FileListEntrySort {
		bool operator()(const FileListEntry *x, const FileListEntry *y) const {
			if (x->mbIsDirectory != y->mbIsDirectory)
				return x->mbIsDirectory;

			return x->mFileName.comparei(y->mFileName) < 0;
		}
	};

	HMENU	mhMenuItemContext;

	uint32	mIconFile;
	uint32	mIconFolder;
	uintptr	mCurrentDirKey;
	bool	mbWriteEnabled;
	bool	mbAutoFlush;

	IATDiskImage *mpImage;
	vdautoptr<IATDiskImage> mpImageAlloc;
	const wchar_t *mpImageName;

	vdautoptr<IATDiskFS> mpFS;
	VDUIProxyListView mList;

	vdrefptr<DropTarget> mpDropTarget;

	HWND	mhwndList;
	VDFunctionThunk *mpListViewThunk;
	WNDPROC mListViewWndProc;

	VDDelegate mDelBeginDrag;
	VDDelegate mDelBeginRDrag;
	VDDelegate mDelContextMenu;
	VDDelegate mDelLabelChanged;
	VDDelegate mDelDoubleClick;

	VDDialogResizerW32 mResizer;
};

void ATUIDialogDiskExplorer::FileListEntry::GetText(int subItem, VDStringW& s) const {
	if (subItem && !mFileKey)
		return;

	switch(subItem) {
		case 0:
			s = mFileName;
			break;

		case 1:
			s.sprintf(L"%u", mSectors);
			break;

		case 2:
			s.sprintf(L"%u", mBytes);
			break;

		case 3:
			if (mbDateValid) {
				s.sprintf(L"%02u/%02u/%02u %02u:%02u:%02u"
					, mDate.mMonth
					, mDate.mDay
					, mDate.mYear % 100
					, mDate.mHour
					, mDate.mMinute
					, mDate.mSecond
					);
			}
			break;
	}
}

ATUIDialogDiskExplorer::ATUIDialogDiskExplorer(IATDiskImage *image, const wchar_t *imageName, bool writeEnabled, bool autoFlush)
	: VDDialogFrameW32(IDD_DISK_EXPLORER)
	, mhMenuItemContext(NULL)
	, mbWriteEnabled(writeEnabled)
	, mbAutoFlush(autoFlush)
	, mpImage(image)
	, mpImageName(imageName)
	, mhwndList(NULL)
	, mpListViewThunk(NULL)
{
	mList.OnItemBeginDrag() += mDelBeginDrag.Bind(this, &ATUIDialogDiskExplorer::OnItemBeginDrag);
	mList.OnItemBeginRDrag() += mDelBeginRDrag.Bind(this, &ATUIDialogDiskExplorer::OnItemBeginDrag);
	mList.OnItemContextMenu() += mDelContextMenu(this, &ATUIDialogDiskExplorer::OnItemContextMenu);
	mList.OnItemLabelChanged() += mDelLabelChanged.Bind(this, &ATUIDialogDiskExplorer::OnItemLabelChanged);
	mList.OnItemDoubleClicked() += mDelDoubleClick.Bind(this, &ATUIDialogDiskExplorer::OnItemDoubleClick);
}

ATUIDialogDiskExplorer::~ATUIDialogDiskExplorer() {
}

bool ATUIDialogDiskExplorer::OnLoaded() {
	SetCurrentSizeAsMinSize();

	HINSTANCE hInst = VDGetLocalModuleHandleW32();
	HICON hIcon = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
	if (hIcon)
		SendMessage(mhdlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

	HICON hSmallIcon = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
	if (hSmallIcon)
		SendMessage(mhdlg, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);

	mResizer.Init(mhdlg);
	mResizer.Add(IDC_FILENAME, VDDialogResizerW32::kTC);
	mResizer.Add(IDC_BROWSE, VDDialogResizerW32::kTR);
	mResizer.Add(IDC_DISK_CONTENTS, VDDialogResizerW32::kMC | VDDialogResizerW32::kAvoidFlicker);
	mResizer.Add(IDC_STATUS, VDDialogResizerW32::kBC);

	mhwndList = GetDlgItem(mhdlg, IDC_DISK_CONTENTS);
	if (mhwndList) {
		mListViewWndProc = (WNDPROC)GetWindowLongPtr(mhwndList, GWLP_WNDPROC);
		mpListViewThunk = VDCreateFunctionThunkFromMethod(this, &ATUIDialogDiskExplorer::ListViewSubclassProc, true);
		if (mpListViewThunk)
			SetWindowLongPtr(mhwndList, GWLP_WNDPROC, (LONG_PTR)mpListViewThunk);
	}

	AddProxy(&mList, IDC_DISK_CONTENTS);

	mList.SetFullRowSelectEnabled(true);
	mList.InsertColumn(0, L"Filename", 0);
	mList.InsertColumn(1, L"Sectors", 0);
	mList.InsertColumn(2, L"Size", 0);
	mList.InsertColumn(3, L"Date", 0);

	mhMenuItemContext = LoadMenu(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDR_DISK_EXPLORER_CONTEXT_MENU));

	mpDropTarget = new DropTarget(mhdlg, this);
	RegisterDragDrop(mList.GetHandle(), mpDropTarget);

	SHFILEINFOW infoFile = {0};
	SHFILEINFOW infoFolder = {0};

	SHGetFileInfoW(L"foo.bin", FILE_ATTRIBUTE_NORMAL, &infoFile, sizeof(infoFile), SHGFI_SMALLICON | SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES);
	HIMAGELIST hil = (HIMAGELIST)SHGetFileInfoW(L"foo.bin", FILE_ATTRIBUTE_DIRECTORY, &infoFolder, sizeof(infoFolder), SHGFI_SMALLICON | SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES);

	mIconFile = infoFile.iIcon;
	mIconFolder = infoFolder.iIcon;

	ListView_SetImageList(mList.GetHandle(), hil, LVSIL_SMALL);

	if (mpImageName) {
		try {
			SetControlText(IDC_FILENAME, mpImageName);
			SendDlgItemMessage(mhdlg, IDC_FILENAME, EM_SETREADONLY, TRUE, FALSE);
			SendDlgItemMessage(mhdlg, IDC_FILENAME, EM_SETSEL, -1, -1);
			EnableControl(IDC_BROWSE, false);

			vdautoptr<IATDiskFS> fs(ATDiskMountImage(mpImage, !mbWriteEnabled));

			if (!fs)
				throw MyError("Unable to detect the file system on the disk image.");

			mpFS.from(fs);
			mpDropTarget->SetFS(mpFS);

			mCurrentDirKey = 0;

			RefreshList();

			if (mbWriteEnabled) {
				if (mpFS->IsReadOnly()) {
					ShowWarning(L"This disk format is only supported in read-only mode.", L"Altirra Warning");
				} else {
					ATDiskFSValidationReport validationReport;
					if (!mpFS->Validate(validationReport)) {
						mpFS->SetReadOnly(true);

						ShowWarning(L"The file system on this disk is damaged and has been mounted as read-only to prevent further damage.", L"Altirra Warning");
					}
				}
			}

			SetFocusToControl(IDC_DISK_CONTENTS);
		} catch(const MyError& e) {
			ShowError(VDTextAToW(e.gets()).c_str(), L"Disk load error");
			End(false);
			return true;
		}
	} else {
		SetFocusToControl(IDC_BROWSE);
	}

	VDDialogFrameW32::OnLoaded();
	return true;
}

void ATUIDialogDiskExplorer::OnDestroy() {
	RevokeDragDrop(mList.GetHandle());
	mpDropTarget.clear();
	mList.Clear();

	if (mhMenuItemContext) {
		DestroyMenu(mhMenuItemContext);
		mhMenuItemContext = NULL;
	}

	if (mhwndList) {
		DestroyWindow(mhwndList);
		mhwndList = NULL;
	}

	if (mpListViewThunk) {
		VDDestroyFunctionThunk(mpListViewThunk);
		mpListViewThunk = NULL;
	}

	VDDialogFrameW32::OnDestroy();
}

void ATUIDialogDiskExplorer::OnSize() {
	mResizer.Relayout();
}

bool ATUIDialogDiskExplorer::OnErase(VDZHDC hdc) {
	mResizer.Erase(&hdc);
	return true;
}

bool ATUIDialogDiskExplorer::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_BROWSE) {
		if (mpImageName)
			return true;

		const VDStringW fn(VDGetLoadFileName(VDMAKEFOURCC('d', 'i', 's', 'k'),
			(VDGUIHandle)mhdlg,
			L"Choose disk image to browse",
			L"All supported images\0*.atr;*.xfd;*.dcm;*.pro;*.atx;*.arc\0"
			L"Atari disk image (*.atr,*.xfd,*.dcm)\0*.atr;*.xfd;*.dcm\0"
			L"Protected disk image (*.pro)\0*.pro\0"
			L"VAPI disk image (*.atx)\0*.atx\0"
			L"Compressed archive (*.arc)\0*.arc\0"
			L"All files\0*.*\0",
			NULL
			));

		if (!fn.empty()) {
			try {
				vdautoptr<IATDiskImage> image;
				vdautoptr<IATDiskFS> fs;
				
				const wchar_t *const fnp = fn.c_str();
				const bool isArc = !vdwcsicmp(VDFileSplitExt(fnp), L".arc");

				if (isArc) {
					fs = ATDiskMountImageARC(fnp);
				} else {
					image = ATLoadDiskImage(fnp);
					fs = ATDiskMountImage(image, false);

					if (!fs)
						throw MyError("Unable to detect the file system on the disk image.");

					mpImageAlloc.from(image);
					mpImage = mpImageAlloc;
				}

				mpFS.from(fs);
				mpDropTarget->SetFS(mpFS);

				mCurrentDirKey = 0;

				SetControlText(IDC_FILENAME, fn.c_str());

				RefreshList();

				if (!isArc && mpFS->IsReadOnly()) {
					ShowWarning(L"This disk format is only supported in read-only mode.", L"Altirra Warning");
				} else {
					ATDiskFSValidationReport validationReport;
					if (!mpFS->Validate(validationReport)) {
						mpFS->SetReadOnly(true);

						ShowWarning(L"The file system on this disk is damaged and has been mounted as read-only to prevent further damage.", L"Altirra Warning");
					}
				}
			} catch(const MyError& e) {
				ShowError(VDTextAToW(e.gets()).c_str(), L"Disk load error");
			}
		}
		return true;
	} else if (id == ID_DISKEXP_RENAME) {
		const int idx = mList.GetSelectedIndex();

		if (idx >= 0)
			mList.EditItemLabel(idx);
	} else if (id == ID_DISKEXP_DELETE) {
		vdfastvector<int> indices;
		mList.GetSelectedIndices(indices);

		if (!indices.empty()) {
			for(vdfastvector<int>::const_iterator it(indices.begin()), itEnd(indices.end());
				it != itEnd;
				++it)
			{
				FileListEntry *fle = static_cast<FileListEntry *>(mList.GetVirtualItem(*it));

				if (fle && fle->mFileKey) {
					try {
						mpFS->DeleteFile(fle->mFileKey);
					} catch(const MyError& e) {
						VDStringW str;
						str.sprintf(L"Cannot delete file \"%ls\": %hs", fle->mFileName.c_str(), e.gets());
						ShowError(str.c_str(), L"Altirra Error");
					}
				}
			}

			OnFSModified();
		}

		return true;
	}

	return false;
}

namespace {
	class GenericDropSource : public IDropSource {
	public:
		GenericDropSource();

		ULONG STDMETHODCALLTYPE AddRef();
		ULONG STDMETHODCALLTYPE Release();
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj);

		HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState);
		HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD dwEffect);

	protected:
		VDAtomicInt mRefCount;
	};

	GenericDropSource::GenericDropSource()
		: mRefCount(0)
	{
	}

	ULONG STDMETHODCALLTYPE GenericDropSource::AddRef() {
		return ++mRefCount;
	}

	ULONG STDMETHODCALLTYPE GenericDropSource::Release() {
		DWORD rc = --mRefCount;

		if (!rc)
			delete this;

		return rc;
	}

	HRESULT STDMETHODCALLTYPE GenericDropSource::QueryInterface(REFIID riid, void **ppvObj) {
		if (riid == IID_IDropSource)
			*ppvObj = static_cast<IDropSource *>(this);
		else if (riid == IID_IUnknown)
			*ppvObj = static_cast<IUnknown *>(this);
		else {
			*ppvObj = NULL;
			return E_NOINTERFACE;
		}

		AddRef();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GenericDropSource::QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) {
		if (fEscapePressed)
			return DRAGDROP_S_CANCEL;

		if (!(grfKeyState & (MK_LBUTTON | MK_RBUTTON)))
			return DRAGDROP_S_DROP;

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GenericDropSource::GiveFeedback(DWORD dwEffect) {
		return DRAGDROP_S_USEDEFAULTCURSORS;
	}
}

namespace {
	class DataObjectFormatEnumerator : public IEnumFORMATETC {
	public:
		DataObjectFormatEnumerator(uint32 fileCount);

		ULONG STDMETHODCALLTYPE AddRef();
		ULONG STDMETHODCALLTYPE Release();
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj);

        HRESULT STDMETHODCALLTYPE Next(ULONG celt, FORMATETC *rgelt, ULONG *pceltFetched);
        HRESULT STDMETHODCALLTYPE Skip(ULONG celt);
        HRESULT STDMETHODCALLTYPE Reset();
        HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC **ppenum);

	private:
		VDAtomicInt mRefCount;

		ULONG mPos;
		uint32 mFileCount;
	};

	DataObjectFormatEnumerator::DataObjectFormatEnumerator(uint32 fileCount)
		: mRefCount(0)
		, mPos(0)
		, mFileCount(fileCount)
	{
	}

	ULONG STDMETHODCALLTYPE DataObjectFormatEnumerator::AddRef() {
		return ++mRefCount;
	}

	ULONG STDMETHODCALLTYPE DataObjectFormatEnumerator::Release() {
		ULONG rc = --mRefCount;

		if (!rc)
			delete this;

		return rc;
	}

	HRESULT STDMETHODCALLTYPE DataObjectFormatEnumerator::QueryInterface(REFIID riid, void **ppvObj) {
		if (riid == IID_IEnumFORMATETC)
			*ppvObj = static_cast<IEnumFORMATETC *>(this);
		else if (riid == IID_IUnknown)
			*ppvObj = static_cast<IUnknown *>(this);
		else {
			*ppvObj = NULL;
			return E_NOINTERFACE;
		}

		AddRef();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DataObjectFormatEnumerator::Next(ULONG celt, FORMATETC *rgelt, ULONG *pceltFetched) {
		ULONG fetched = 0;

		memset(rgelt, 0, sizeof(rgelt[0])*celt);

		while(celt && mPos < 3) {
			switch(mPos) {
				case 0:
					rgelt->cfFormat = gCFDescriptorA;
					rgelt->dwAspect = DVASPECT_CONTENT;
					rgelt->lindex = -1;
					rgelt->ptd = NULL;
					rgelt->tymed = TYMED_HGLOBAL;
					break;

				case 1:
					rgelt->cfFormat = gCFDescriptorW;
					rgelt->dwAspect = DVASPECT_CONTENT;
					rgelt->lindex = -1;
					rgelt->ptd = NULL;
					rgelt->tymed = TYMED_HGLOBAL;
					break;

				default:
					rgelt->cfFormat = gCFContents;
					rgelt->dwAspect = DVASPECT_CONTENT;
					rgelt->lindex = mPos - 1;
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

	HRESULT STDMETHODCALLTYPE DataObjectFormatEnumerator::Skip(ULONG celt) {
		if (!celt)
			return S_OK;

		if (mPos > mFileCount || (mFileCount + 1) - mPos < celt) {
			mPos = mFileCount + 1;
			return S_FALSE;
		}

		mPos += celt;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DataObjectFormatEnumerator::Reset() {
		mPos = 0;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DataObjectFormatEnumerator::Clone(IEnumFORMATETC **ppenum) {
		DataObjectFormatEnumerator *clone = new_nothrow DataObjectFormatEnumerator(mFileCount);
		*ppenum = clone;

		if (!clone)
			return E_OUTOFMEMORY;

		clone->mPos = mPos;
		clone->AddRef();
		return S_OK;
	}
}

namespace {
	class DataObject : public IDataObject {
	public:
		DataObject(IATDiskFS *fs);
		~DataObject();

		void AddFile(const ATUIDiskExplorerFileEntry *fle);

		ULONG STDMETHODCALLTYPE AddRef();
		ULONG STDMETHODCALLTYPE Release();
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj);

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

		IATDiskFS *mpFS;

		vdfastvector<const ATUIDiskExplorerFileEntry *> mFiles;
		VDAtomicInt mRefCount;
	};

	DataObject::DataObject(IATDiskFS *fs)
		: mRefCount(0)
		, mpFS(fs)
	{
		InitFileDropClipboardFormats();
	}

	DataObject::~DataObject() {
	}

	ULONG STDMETHODCALLTYPE DataObject::AddRef() {
		return ++mRefCount;
	}

	ULONG STDMETHODCALLTYPE DataObject::Release() {
		DWORD rc = --mRefCount;

		if (!rc)
			delete this;

		return rc;
	}

	HRESULT STDMETHODCALLTYPE DataObject::QueryInterface(REFIID riid, void **ppvObj) {
		if (riid == IID_IDataObject)
			*ppvObj = static_cast<IDataObject *>(this);
		else if (riid == IID_IUnknown)
			*ppvObj = static_cast<IUnknown *>(this);
		else {
			*ppvObj = NULL;
			return E_NOINTERFACE;
		}

		AddRef();
		return S_OK;
	}

	void DataObject::AddFile(const ATUIDiskExplorerFileEntry *fle) {
		mFiles.push_back(fle);
	}

	HRESULT STDMETHODCALLTYPE DataObject::GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) {
		HRESULT hr = QueryGetData(pformatetcIn);

		if (FAILED(hr))
			return hr;

		if (pformatetcIn->cfFormat == gCFDescriptorA) {
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
		} else if (pformatetcIn->cfFormat == gCFDescriptorW) {
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
		} else {
			vdrefptr<IStream> stream;
			hr = CreateStreamOnHGlobal(NULL, TRUE, ~stream);
			if (FAILED(hr))
				return STG_E_MEDIUMFULL;

			const ATUIDiskExplorerFileEntry *fle = mFiles[pformatetcIn->lindex];

			vdfastvector<uint8> buf;

			pmedium->tymed = TYMED_ISTREAM;
			pmedium->pstm = NULL;
			pmedium->pUnkForRelease = NULL;

			try {
				mpFS->ReadFile(fle->mFileKey, buf);
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

			stream->Write(buf.data(), buf.size(), NULL);

			// Required for Directory Opus to work.
			LARGE_INTEGER lizero = {0};
			stream->Seek(lizero, STREAM_SEEK_SET, NULL);

			pmedium->pstm = stream.release();
		}

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DataObject::GetDataHere(FORMATETC *pformatetc, STGMEDIUM *pmedium) {
		HRESULT hr = QueryGetData(pformatetc);

		if (FAILED(hr))
			return hr;

		if (pformatetc->cfFormat == gCFDescriptorA) {
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
		} else if (pformatetc->cfFormat == gCFDescriptorW) {
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
		} else if (pformatetc->cfFormat == gCFContents) {
			vdrefptr<IStream> streamAlloc;
			IStream *stream;

			const ATUIDiskExplorerFileEntry *fle = mFiles[pformatetc->lindex];

			if (pmedium->tymed == TYMED_HGLOBAL) {
				if (GlobalSize(pmedium->hGlobal) < fle->mBytes)
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
				mpFS->ReadFile(fle->mFileKey, buf);
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

			stream->Write(buf.data(), buf.size(), NULL);
		}

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DataObject::QueryGetData(FORMATETC *pformatetc) {
		if (pformatetc->cfFormat == gCFContents) {
			if ((uint32)pformatetc->lindex >= mFiles.size())
				return DV_E_LINDEX;

			if (!(pformatetc->tymed & (TYMED_ISTREAM | TYMED_HGLOBAL)))
				return DV_E_TYMED;
		} else if (pformatetc->cfFormat == gCFDescriptorA || pformatetc->cfFormat == gCFDescriptorW) {
			if (pformatetc->lindex != -1)
				return DV_E_LINDEX;

			if (!(pformatetc->tymed & TYMED_HGLOBAL))
				return DV_E_TYMED;
		} else
			return DV_E_CLIPFORMAT;

		if (pformatetc->dwAspect != DVASPECT_CONTENT)
			return DV_E_DVASPECT;

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DataObject::GetCanonicalFormatEtc(FORMATETC *pformatetcIn, FORMATETC *pformatetcOut) {
		*pformatetcOut = *pformatetcIn;
		pformatetcOut->ptd = NULL;

		return DATA_S_SAMEFORMATETC;
	}

	HRESULT STDMETHODCALLTYPE DataObject::SetData(FORMATETC *pformatetc, STGMEDIUM *pmedium, BOOL fRelease) {
		return E_FAIL;
	}

	HRESULT STDMETHODCALLTYPE DataObject::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc) {
		if (dwDirection != DATADIR_GET) {
			*ppenumFormatEtc = NULL;
			return E_NOTIMPL;
		}

		*ppenumFormatEtc = new_nothrow DataObjectFormatEnumerator((uint32)mFiles.size());
		if (!*ppenumFormatEtc)
			return E_OUTOFMEMORY;

		(*ppenumFormatEtc)->AddRef();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DataObject::DAdvise(FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection) {
		return OLE_E_ADVISENOTSUPPORTED;
	}

	HRESULT STDMETHODCALLTYPE DataObject::DUnadvise(DWORD dwConnection) {
		return OLE_E_ADVISENOTSUPPORTED;
	}

	HRESULT STDMETHODCALLTYPE DataObject::EnumDAdvise(IEnumSTATDATA **ppenumAdvise) {
		return OLE_E_ADVISENOTSUPPORTED;
	}

	void DataObject::GenerateFileDescriptors(FILEGROUPDESCRIPTORA *group) {
		uint32 n = mFiles.size();
		group->cItems = (DWORD)n;

		for(uint32 i=0; i<n; ++i) {
			const ATUIDiskExplorerFileEntry *fle = mFiles[i];
			FILEDESCRIPTORA *fd = &group->fgd[i];

			memset(fd, 0, sizeof *fd);
			
			fd->dwFlags = FD_FILESIZE | FD_ATTRIBUTES;
			fd->nFileSizeLow = fle->mBytes;
			fd->nFileSizeHigh = 0;
			fd->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;

			vdstrlcpy(fd->cFileName, VDTextWToA(fle->mFileName).c_str(), MAX_PATH);
		}
	}

	void DataObject::GenerateFileDescriptors(FILEGROUPDESCRIPTORW *group) {
		uint32 n = mFiles.size();
		group->cItems = (DWORD)n;

		for(uint32 i=0; i<n; ++i) {
			const ATUIDiskExplorerFileEntry *fle = mFiles[i];
			FILEDESCRIPTORW *fd = &group->fgd[i];

			memset(fd, 0, sizeof *fd);
			
			fd->dwFlags = FD_FILESIZE | FD_ATTRIBUTES;
			fd->nFileSizeLow = fle->mBytes;
			fd->nFileSizeHigh = 0;
			fd->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;

			vdwcslcpy(fd->cFileName, fle->mFileName.c_str(), MAX_PATH);
		}
	}
}

void ATUIDialogDiskExplorer::OnItemBeginDrag(VDUIProxyListView *sender, int item) {
	vdrefptr<DataObject> dataObject(new DataObject(mpFS));

	vdfastvector<int> indices;
	mList.GetSelectedIndices(indices);

	for(vdfastvector<int>::const_iterator it(indices.begin()), itEnd(indices.end());
		it != itEnd;
		++it)
	{
		FileListEntry *fle = static_cast<FileListEntry *>(mList.GetVirtualItem(*it));

		if (fle->mFileKey)
			dataObject->AddFile(fle);
	}

	vdrefptr<IDropSource> dropSource(new GenericDropSource);
	DWORD srcEffects = 0;

	DoDragDrop(dataObject, dropSource, DROPEFFECT_COPY | DROPEFFECT_MOVE, &srcEffects);
}

void ATUIDialogDiskExplorer::OnItemContextMenu(VDUIProxyListView *sender, const VDUIProxyListView::ContextMenuEvent& event) {
	if (!mhMenuItemContext)
		return;

	TrackPopupMenu(GetSubMenu(mhMenuItemContext, 0), TPM_LEFTALIGN | TPM_TOPALIGN, event.mX, event.mY, 0, mhdlg, NULL);
}

void ATUIDialogDiskExplorer::OnItemLabelChanged(VDUIProxyListView *sender, VDUIProxyListView::LabelChangedEvent *event) {
	int idx = event->mIndex;

	FileListEntry *fle = static_cast<FileListEntry *>(mList.GetVirtualItem(idx));
	if (!fle || !fle->mFileKey)
		return;

	try {
		mpFS->RenameFile(fle->mFileKey, VDTextWToA(event->mpNewLabel).c_str());

		ATDiskFSEntryInfo einfo;
		mpFS->GetFileInfo(fle->mFileKey, einfo);
		fle->InitFrom(einfo);

		OnFSModified();
	} catch(const MyError& e) {
		event->mbAllowEdit = false;

		VDStringW s;
		s.sprintf(L"Cannot rename \"%ls\" to \"%ls\": %hs", fle->mFileName.c_str(), event->mpNewLabel, e.gets());
		ShowError(s.c_str(), L"Altirra Error");
	}
}

void ATUIDialogDiskExplorer::OnItemDoubleClick(VDUIProxyListView *sender, int item) {
	FileListEntry *fle = static_cast<FileListEntry *>(mList.GetVirtualItem(item));
	if (!fle)
		return;

	if (!fle->mbIsDirectory)
		return;

	if (fle->mFileKey)
		mCurrentDirKey = fle->mFileKey;
	else
		mCurrentDirKey = mpFS->GetParentDirectory(mCurrentDirKey);

	RefreshList();
}

void ATUIDialogDiskExplorer::OnFSModified() {
	mpFS->Flush();

	if (mbAutoFlush && mpImage)
		mpImage->Flush();

	RefreshList();
}

void ATUIDialogDiskExplorer::RefreshList() {
	mList.Clear();

	if (!mpFS)
		return;

	if (mCurrentDirKey) {
		vdrefptr<FileListEntry> fle(new FileListEntry);

		fle->mFileName = L"..";
		fle->mFileKey = 0;
		fle->mbIsDirectory = true;

		int item = mList.InsertVirtualItem(-1, fle);
		if (item >= 0)
			mList.SetItemImage(item, mIconFolder);
	}

	// Read directory
	ATDiskFSEntryInfo einfo;

	uintptr searchKey = mpFS->FindFirst(mCurrentDirKey, einfo);

	vdfastvector<FileListEntry *> fles;

	try {
		if (searchKey) {
			do {
				vdrefptr<FileListEntry> fle(new FileListEntry);

				fle->InitFrom(einfo);

				fles.push_back(fle.release());
			} while(mpFS->FindNext(searchKey, einfo));

			mpFS->FindEnd(searchKey);
		}

		std::sort(fles.begin(), fles.end(), FileListEntrySort());

		for(vdfastvector<FileListEntry *>::const_iterator it(fles.begin()), itEnd(fles.end());
			it != itEnd;
			++it)
		{
			FileListEntry *fle = *it;

			int item = mList.InsertVirtualItem(-1, fle);
			if (item >= 0)
				mList.SetItemImage(item, fle->mbIsDirectory ? mIconFolder : mIconFile);
		}
	} catch(...) {
		VDReleaseObjects(fles);
		throw;
	}

	VDReleaseObjects(fles);

	mList.AutoSizeColumns();

	ATDiskFSInfo fsinfo;
	mpFS->GetInfo(fsinfo);

	VDStringW s;
	s.sprintf(L"Mounted %hs file system%hs. %d blocks free", fsinfo.mFSType.c_str(), mpFS->IsReadOnly() ? " (read-only)" : "", fsinfo.mFreeBlocks);

	SetControlText(IDC_STATUS, s.c_str());
}

LRESULT ATUIDialogDiskExplorer::ListViewSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_MOUSEACTIVATE)
		return MA_NOACTIVATE;

	return CallWindowProc(mListViewWndProc, hwnd, msg, wParam, lParam);
}

///////////////////////////////////////////////////////////////////////////

void ATUIShowDialogDiskExplorer(VDGUIHandle h) {
	ATUIDialogDiskExplorer dlg;

	dlg.ShowDialog(h);
}

void ATUIShowDialogDiskExplorer(VDGUIHandle h, IATDiskImage *image, const wchar_t *imageName, bool writeEnabled, bool autoFlush) {
	ATUIDialogDiskExplorer dlg(image, imageName, writeEnabled, autoFlush);

	dlg.ShowDialog(h);
}
