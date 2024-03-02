//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2019 Avery Lee
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

#pragma warning(push)
#pragma warning(disable: 4768)		// ShlObj.h(1065): warning C4768: __declspec attributes before linkage specification are ignored
#pragma warning(disable: 4091)		// shlobj.h(1151): warning C4091: 'typedef ': ignored on left of 'tagGPFIDL_FLAGS' when no variable is declared
#include <shlobj.h>
#pragma warning(pop)

#include <shellapi.h>
#include <ole2.h>
#include <windows.h>
#include <richedit.h>
#include <commoncontrols.h>
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/event.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/registry.h>
#include <vd2/system/strutil.h>
#include <vd2/system/thunk.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/services.h>
#include "oshelper.h"
#include "resource.h"
#include <at/atcore/blockdevice.h>
#include <at/atio/diskimage.h>
#include <at/atio/diskfs.h>
#include <at/atio/partitiondiskview.h>
#include <at/atio/partitiontable.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/genericdialog.h>
#include <at/atnativeui/uiproxies.h>
#include "uidiskexplorer_win32.h"
#include "uifilefilters.h"
#include "disk.h"
#include "simulator.h"

extern ATSimulator g_sim;

class ATUIFileViewer : public VDResizableDialogFrameW32 {
public:
	ATUIFileViewer();

	void SetBuffer(const void *buf, size_t len);

protected:
	bool OnLoaded() override;
	void OnDestroy() override;
	void OnWrapModeChanged(VDUIProxyComboBoxControl *sender, int sel);
	void ReloadFile();

	const void *mpSrc;
	size_t mSrcLen;
	int mViewMode;

	enum ViewMode {
		kViewMode_None,
		kViewMode_Window,
		kViewMode_38Columns,
		kViewMode_HexDump,
		kViewMode_Executable,
		kViewModeCount
	};

	VDUIProxyComboBoxControl mViewModeCombo;
	VDDelegate mDelSelChanged;
};

ATUIFileViewer::ATUIFileViewer()
	: VDResizableDialogFrameW32(IDD_FILEVIEW)
	, mpSrc(nullptr)
	, mSrcLen(0)
{
	mViewModeCombo.OnSelectionChanged() += mDelSelChanged.Bind(this, &ATUIFileViewer::OnWrapModeChanged);
}

void ATUIFileViewer::SetBuffer(const void *buf, size_t len) {
	mpSrc = buf;
	mSrcLen = len;
}

bool ATUIFileViewer::OnLoaded() {
	AddProxy(&mViewModeCombo, IDC_VIEWMODE);
	mViewModeCombo.AddItem(L"Text: no line wrapping");
	mViewModeCombo.AddItem(L"Text: wrap to window");
	mViewModeCombo.AddItem(L"Text: wrap to GR.0 screen (38 columns)");
	mViewModeCombo.AddItem(L"Hex dump");
	mViewModeCombo.AddItem(L"Executable");

	VDRegistryAppKey key("Settings", false);
	mViewMode = (ViewMode)key.getEnumInt("File Viewer: View mode", kViewModeCount, (int)kViewMode_38Columns);

	mViewModeCombo.SetSelection(mViewMode);

	mResizer.Add(IDC_RICHEDIT, mResizer.kMC | mResizer.kAvoidFlicker | mResizer.kSuppressFontChange);
	
	ATUIRestoreWindowPlacement(mhdlg, "File viewer", SW_SHOW);

	HWND hwndHelp = GetDlgItem(mhdlg, IDC_RICHEDIT);
	if (hwndHelp) {
		RECT r;
		SendMessage(hwndHelp, EM_GETRECT, 0, (LPARAM)&r);
		r.left += 4;
		r.top += 4;
		r.right -= 4;
		r.bottom -= 4;
		SendMessage(hwndHelp, EM_SETRECT, 0, (LPARAM)&r);

		ReloadFile();
	}

	return true;
}

void ATUIFileViewer::OnDestroy() {
	VDRegistryAppKey key("Settings");
	key.setInt("File Viewer: View mode", (int)mViewMode);

	ATUISaveWindowPlacement(mhdlg, "File viewer");

	VDDialogFrameW32::OnDestroy();
}

void ATUIFileViewer::OnWrapModeChanged(VDUIProxyComboBoxControl *sender, int sel) {
	if (sel >= 0 && sel < kViewModeCount) {
		if (mViewMode != sel) {
			mViewMode = sel;
			ReloadFile();
		}
	}
}

void ATUIFileViewer::ReloadFile() {
	HWND hwndHelp = GetDlgItem(mhdlg, IDC_RICHEDIT);
	if (!hwndHelp)
		return;

	SendMessageW(hwndHelp, WM_SETREDRAW, FALSE, 0);

	DWORD dwStyles = GetWindowLong(hwndHelp, GWL_STYLE);
	if (mViewMode == kViewMode_None || mViewMode == kViewMode_HexDump) {
		SetWindowLong(hwndHelp, GWL_STYLE, dwStyles | WS_HSCROLL | ES_AUTOHSCROLL);

		// This is voodoo from the Internet....
		SendMessage(hwndHelp, EM_SETTARGETDEVICE, 0, 1);
	} else {
		SetWindowLong(hwndHelp, GWL_STYLE, dwStyles & ~(WS_HSCROLL | ES_AUTOHSCROLL));
		SendMessage(hwndHelp, EM_SETTARGETDEVICE, 0, 0);
	}

	SetWindowPos(hwndHelp, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

	// use approx GR.0 colors
	const uint32 background = 0xB34700;
	const uint32 foreground = 0xFFFBC9;
	SendMessage(hwndHelp, EM_SETBKGNDCOLOR, FALSE, background);

	VDStringA rtf;
		
	rtf.sprintf(R"raw({\rtf\ansi\deff0
{\fonttbl{\f0\fmodern Lucida Console;}}
{\colortbl;\red%u\green%u\blue%u;\red%u\green%u\blue%u;}
\fs16\cf2 )raw"
	, background & 255
	, (background >> 8) & 255
	, (background >> 16) & 255
	, foreground & 255
	, (foreground >> 8) & 255
	, (foreground >> 16) & 255
	);

	const uint8 *src = (const uint8 *)mpSrc;
	bool inv = false;
	int col = 0;
	int linewidth = INT_MAX;

	if (mViewMode == kViewMode_38Columns)
		linewidth = 38;

	if (mViewMode == kViewMode_Executable) {
		uint32_t pos = 0;

		while(pos + 4 <= mSrcLen) {
			uint32_t start = VDReadUnalignedLEU16(&src[pos]);

			if (start == 0xFFFF) {
				rtf.append_sprintf("%08X: $FFFF\\line ", pos);
				pos += 2;
				continue;
			}

			uint32_t end = VDReadUnalignedLEU16(&src[pos + 2]);
			uint32_t len = end + 1 - start;

			if (end < start || mSrcLen - pos < len) {
				rtf.append_sprintf("%08X: Invalid range %04X-%04X\\line ", pos, start, end);

				// dump the remainder of the file
				pos += 4;
				len = (uint32)(mSrcLen - pos);
			} else if (start == 0x2E0 && end == 0x2E1) {
				rtf.append_sprintf("%08X: Run $%04X\\line ", pos, VDReadUnalignedLEU16(&src[pos+4]));
				pos += 6;
				continue;
			} else if (start == 0x2E2 && end == 0x2E3) {
				rtf.append_sprintf("%08X: Init $%04X\\line ", pos, VDReadUnalignedLEU16(&src[pos+4]));
				pos += 6;
				continue;
			} else {
				rtf.append_sprintf("%08X: Load $%04X-%04X\\line ", pos, start, end);
				pos += 4;
			}

			while(len) {
				uint32_t tc = len > 16 ? 16 : len;

				rtf.append_sprintf("%08X / $%04X:", pos, start);

				for(uint32_t i=0; i<tc; ++i)
					rtf.append_sprintf(" %02X", src[pos+i]);

				rtf.append("\\line ");

				pos += tc;
				len -= tc;
				start += tc;
			}
		}
	} else for(size_t i=0; i<mSrcLen; ++i) {
		unsigned char c = src[i];

		if (mViewMode == kViewMode_HexDump && !(i & 15)) {
			if (inv) {
				inv = false;

				rtf += "}";
			}

			rtf.append_sprintf("%08X:", i);

			for(size_t j = 0; j < 16; ++j) {
				if (i + j < mSrcLen)
					rtf.append_sprintf("%c%02X", j == 8 ? '-' : ' ', src[i+j]);
				else
					rtf += "   ";
			}

			rtf += " | ";
			col = 0;
		}

		if (c == 0x9B && mViewMode != kViewMode_HexDump) {
			rtf += "\\line ";
			col = 0;
		} else {
			bool newinv = (c >= 0x80);

			if (inv != newinv) {
				if (newinv)
					rtf += "{\\highlight2\\cf1 ";
				else
					rtf += "}";

				inv = newinv;
			}

			c &= 0x7f;

			if (c == 0x20) {
				if (inv)
					rtf += "\\'A0";
				else
					rtf += ' ';
			} else if (c < 0x20) {
				static const uint16 kLowTable[]={
					0x2665,	// heart
					0x251C,	// vertical tee right
					0x2595,	// vertical bar right
					0x2518,	// top-left elbow
					0x2524,	// vertical tee left
					0x2510,	// bottom-left elbow
					0x2571,	// forward diagonal
					0x2572,	// backwards diagonal
					0x25E2,	// lower right filled triangle
					0x2597,	// lower right quadrant
					0x25E3,	// lower left filled triangle
					0x259D,	// quadrant upper right
					0x2598,	// quadrant upper left
					0x2594,	// top quarter
					0x2582,	// bottom quarter
					0x2596,	// lower left quadrant
					
					0x2663,	// club
					0x250C,	// lower-right elbow
					0x2500,	// horizontal bar
					0x253C,	// four-way
					0x2022,	// filled circle
					0x2584,	// lower half
					0x258E,	// left quarter
					0x252C,	// horizontal tee down
					0x2534,	// horizontal tee up
					0x258C,	// left side
					0x2514,	// top-right elbow
					0x241B,	// escape
					0x2191,	// up arrow
					0x2193,	// down arrow
					0x2190,	// left arrow
					0x2192,	// right arrow
				};

				rtf.append_sprintf("\\u%u?", kLowTable[c]);
			} else if (c == 0x60) {
				rtf += "\\u9830?"; // U+2666 black diamond suit
			} else if (c >= 0x7B) {
				static const uint16 kHighTable[]={
					0x2660,	// spade
					'|',	// vertical bar (leave this alone so as to not invite font issues)
					0x21B0,	// curved arrow up-left
					0x25C0,	// left arrow
					0x25B6,	// right arrow
				};
				
				rtf.append_sprintf("\\u%u?", kHighTable[c - 0x7B]);
			} else if (c == '{' || c == '}' || c == '\\')
				rtf.append_sprintf("\\'%02x", c);
			else
				rtf += (char)c;

			if (++col >= linewidth) {
				rtf += "\\line ";
				col = 0;
			}
		}

		if (mViewMode == kViewMode_HexDump) {
			if (i + 1 == mSrcLen || (i & 15) == 15)
				rtf += "\\line ";
		}
	}

	if (inv)
		rtf += "}";

	rtf += "}";

	SETTEXTEX stex;
	stex.flags = ST_DEFAULT;
	stex.codepage = 1252;
	SendMessageW(hwndHelp, EM_SETTEXTEX, (WPARAM)&stex, (LPARAM)rtf.c_str());

	SetFocusToControl(IDC_RICHEDIT);
	SendMessageW(hwndHelp, EM_SETSEL, 0, 0);

	SendMessageW(hwndHelp, WM_SETREDRAW, TRUE, 0);
	RedrawWindow(hwndHelp, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

///////////////////////////////////////////////////////////////////////////

struct ATUIDiskExplorerFileEntry {
public:
	VDStringW mFileName;
	ATDiskFSKey mFileKey;
	uint32 mSectors;
	uint32 mBytes;
	bool mbIsDirectory;
	bool mbIsCreate;
	bool mbDateValid;
	VDExpandedDate mDate;
};

class ATUIDialogDiskExplorer final : public VDResizableDialogFrameW32, public IATDropTargetNotify {
public:
	ATUIDialogDiskExplorer(IATDiskImage *image = NULL, const wchar_t *imageName = NULL, bool writeEnabled = true, bool autoFlush = true, ATDiskInterface *di = nullptr, IATBlockDevice *dev = nullptr);
	~ATUIDialogDiskExplorer();

protected:
	class ListEntry;
	class FileListEntry;
	struct FileListEntrySort;
	class PartitionListEntry;

	bool OnLoaded();
	void OnDestroy();
	bool OnErase(VDZHDC hdc);
	void OnInitMenu(VDZHMENU hmenu);
	bool OnCommand(uint32 id, uint32 extcode);

	void OnItemBeginDrag(VDUIProxyListView *sender, int item);
	void OnItemContextMenu(VDUIProxyListView *sender, VDUIProxyListView::ContextMenuEvent& event);
	void OnItemLabelChanged(VDUIProxyListView *sender, VDUIProxyListView::LabelChangedEvent *event);
	void OnItemDoubleClick(VDUIProxyListView *sender, int item);

	void OpenPartition(const PartitionListEntry& ple);

	ListEntry *GetListEntry(int idx);
	FileListEntry *GetFileListEntry(int idx);
	PartitionListEntry *GetPartitionListEntry(int idx);

	virtual ATDiskFSKey GetDropTargetParentKey() const { return mCurrentDirKey; }
	void OnFSModified();

	void MountFS(ATDiskInterface *diskInterface, IATDiskImage& image, vdautoptr<IATDiskFS> fs, const wchar_t *fsDescription, bool write, bool autoflush);
	void ValidateForWrites();
	void RefreshList();
	void RefreshListFromPartitions();
	void RefreshListFromFileSystem();
	void RefreshFsInfo();
	void WriteFile(const char *filename, const void *data, uint32 len, const VDDate& creationTime);

	LRESULT ListViewSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	HMENU	mhMenuItemContext = nullptr;

	uint32	mIconFile = 0;
	uint32	mIconFolder = 0;
	uint32	mIconPartition = 0;
	ATDiskFSKey	mCurrentDirKey;
	bool	mbWriteEnabled = false;
	bool	mbAutoFlush = false;
	bool	mbAdjustFilenames = true;
	bool	mbStrictFilenames = true;

	IATDiskImage *mpImage = nullptr;
	vdrefptr<IATDiskImage> mpImageAlloc;
	const wchar_t *mpImageName = nullptr;
	ATDiskInterface *mpDiskInterface = nullptr;
	IATBlockDevice *mpBlockDevice = nullptr;

	vdautoptr<IATDiskFS> mpFS;
	VDUIProxyListView mList;
	VDUIProxyEditControl mFileNameView;

	vdrefptr<IATUIDiskExplorerDropTargetW32> mpDropTarget;

	HWND	mhwndList = nullptr;
	VDFunctionThunkInfo *mpListViewThunk = nullptr;
	WNDPROC mListViewWndProc = nullptr;

	VDDelegate mDelBeginDrag;
	VDDelegate mDelBeginRDrag;
	VDDelegate mDelContextMenu;
	VDDelegate mDelLabelChanged;
	VDDelegate mDelDoubleClick;
};

class ATUIDialogDiskExplorer::ListEntry : public vdrefcounted<IVDUIListViewVirtualItem>, public IVDUnknown {
};

class ATUIDialogDiskExplorer::FileListEntry : public ListEntry, public ATUIDiskExplorerFileEntry {
public:
	static constexpr uint32 kTypeID = "ATUIDialogDiskExplorer::FileListEntry"_vdtypeid;

	void *AsInterface(uint32 iid) override { return iid == kTypeID ? this : nullptr; }

	void InitFrom(const ATDiskFSEntryInfo& einfo);

	void GetText(int subItem, VDStringW& s) const;
};

class ATUIDialogDiskExplorer::PartitionListEntry : public ListEntry, public ATPartitionInfo {
public:
	static constexpr uint32 kTypeID = "ATUIDialogDiskExplorer::PartitionListEntry"_vdtypeid;

	void *AsInterface(uint32 iid) override { return iid == kTypeID ? this : nullptr; }

	void InitFrom(const ATPartitionInfo& pi);

	void GetText(int subItem, VDStringW& s) const;
};

struct ATUIDialogDiskExplorer::FileListEntrySort {
	bool operator()(const FileListEntry *x, const FileListEntry *y) const {
		if (x->mbIsDirectory != y->mbIsDirectory)
			return x->mbIsDirectory;

		return x->mFileName.comparei(y->mFileName) < 0;
	}
};

void ATUIDialogDiskExplorer::FileListEntry::InitFrom(const ATDiskFSEntryInfo& einfo) {
	mFileName = VDTextAToW(einfo.mFileName);
	mSectors = einfo.mSectors;
	mBytes = einfo.mBytes;
	mFileKey = einfo.mKey;
	mbIsDirectory = einfo.mbIsDirectory;
	mbIsCreate = false;
	mbDateValid = einfo.mbDateValid;
	mDate = einfo.mDate;
}

void ATUIDialogDiskExplorer::FileListEntry::GetText(int subItem, VDStringW& s) const {
	if (subItem && mFileKey == ATDiskFSKey::None)
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

void ATUIDialogDiskExplorer::PartitionListEntry::InitFrom(const ATPartitionInfo& pi) {
	static_cast<ATPartitionInfo&>(*this) = pi;
}

void ATUIDialogDiskExplorer::PartitionListEntry::GetText(int subItem, VDStringW& s) const {
	switch(subItem) {
		case 0:
			s = mName;
			break;

		case 1:
			s.sprintf(L"%u", mBlockCount);
			break;

		case 2:
			s.sprintf(L"%u", mSectorCount * mSectorSize);
			break;

		case 3:
			break;
	}
}

ATUIDialogDiskExplorer::ATUIDialogDiskExplorer(IATDiskImage *image, const wchar_t *imageName, bool writeEnabled, bool autoFlush, ATDiskInterface *di, IATBlockDevice *dev)
	: VDResizableDialogFrameW32(IDD_DISK_EXPLORER)
	, mhMenuItemContext(NULL)
	, mbWriteEnabled(writeEnabled)
	, mbAutoFlush(autoFlush)
	, mpImage(image)
	, mpImageName(imageName)
	, mpDiskInterface(di)
	, mpBlockDevice(dev)
	, mhwndList(NULL)
	, mpListViewThunk(NULL)
{
	mList.OnItemBeginDrag() += mDelBeginDrag.Bind(this, &ATUIDialogDiskExplorer::OnItemBeginDrag);
	mList.OnItemBeginRDrag() += mDelBeginRDrag.Bind(this, &ATUIDialogDiskExplorer::OnItemBeginDrag);
	mList.OnItemContextMenu() += mDelContextMenu.Bind(this, &ATUIDialogDiskExplorer::OnItemContextMenu);
	mList.OnItemLabelChanged() += mDelLabelChanged.Bind(this, &ATUIDialogDiskExplorer::OnItemLabelChanged);
	mList.OnItemDoubleClicked() += mDelDoubleClick.Bind(this, &ATUIDialogDiskExplorer::OnItemDoubleClick);
}

ATUIDialogDiskExplorer::~ATUIDialogDiskExplorer() {
}

bool ATUIDialogDiskExplorer::OnLoaded() {
	VDRegistryAppKey key("Settings", false);
	mbStrictFilenames = key.getBool("Disk Explorer: Strict filenames", mbStrictFilenames);
	mbAdjustFilenames = key.getBool("Disk Explorer: Adjust filenames", mbAdjustFilenames);

	HINSTANCE hInst = VDGetLocalModuleHandleW32();
	HICON hIcon = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
	if (hIcon)
		SendMessage(mhdlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

	HICON hSmallIcon = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
	if (hSmallIcon)
		SendMessage(mhdlg, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);

	mResizer.Add(IDC_FILENAME, VDDialogResizerW32::kTC);
	mResizer.Add(IDC_BROWSE, VDDialogResizerW32::kTR);
	mResizer.Add(IDC_DISK_CONTENTS, VDDialogResizerW32::kMC | VDDialogResizerW32::kAvoidFlicker);
	mResizer.Add(IDC_STATUS, VDDialogResizerW32::kBC);

	mhwndList = GetDlgItem(mhdlg, IDC_DISK_CONTENTS);
	if (mhwndList) {
		mListViewWndProc = (WNDPROC)GetWindowLongPtr(mhwndList, GWLP_WNDPROC);
		mpListViewThunk = VDCreateFunctionThunkFromMethod(this, &ATUIDialogDiskExplorer::ListViewSubclassProc, true);
		if (mpListViewThunk)
			SetWindowLongPtr(mhwndList, GWLP_WNDPROC, (LONG_PTR)VDGetThunkFunction<WNDPROC>(mpListViewThunk));
	}

	AddProxy(&mList, IDC_DISK_CONTENTS);
	AddProxy(&mFileNameView, IDC_FILENAME);

	mList.SetActivateOnEnterEnabled(true);
	mList.SetFullRowSelectEnabled(true);
	mList.InsertColumn(0, L"Filename", 0);
	mList.InsertColumn(1, L"Sectors", 0);
	mList.InsertColumn(2, L"Size", 0);
	mList.InsertColumn(3, L"Creation Date", 0);

	mhMenuItemContext = LoadMenu(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDR_DISK_EXPLORER_CONTEXT_MENU));

	ATUICreateDiskExplorerDropTargetW32(mhdlg, this, ~mpDropTarget);
	RegisterDragDrop(mList.GetHandle(), mpDropTarget->AsDropTarget());

	if (SHSTOCKICONINFO ssii = {sizeof(SHSTOCKICONINFO)};
		SUCCEEDED(SHGetStockIconInfo(SIID_DOCNOASSOC, SHGSI_SYSICONINDEX | SHGSI_SMALLICON, &ssii)))
	{
		mIconFile = ssii.iSysImageIndex;
	}

	if (SHSTOCKICONINFO ssii = {sizeof(SHSTOCKICONINFO)};
		SUCCEEDED(SHGetStockIconInfo(SIID_FOLDER, SHGSI_SYSICONINDEX | SHGSI_SMALLICON, &ssii)))
	{
		mIconFolder = ssii.iSysImageIndex;
	}

	if (SHSTOCKICONINFO ssii = {sizeof(SHSTOCKICONINFO)};
		SUCCEEDED(SHGetStockIconInfo(SIID_DRIVEFIXED, SHGSI_SYSICONINDEX | SHGSI_SMALLICON, &ssii)))
	{
		mIconPartition = ssii.iSysImageIndex;
	}

	HIMAGELIST hil = nullptr;
	if (SUCCEEDED(SHGetImageList(SHIL_SMALL, IID_IImageList, (void **)&hil)))
		ListView_SetImageList(mList.GetHandle(), hil, LVSIL_SMALL);

	if (mpImage) {
		try {
			mFileNameView.SetReadOnly(true);
			EnableControl(IDC_BROWSE, false);

			vdautoptr<IATDiskFS> fs(ATDiskMountImage(mpImage, !mbWriteEnabled));

			if (!fs)
				throw MyError("Unable to detect the file system on the disk image.");

			auto image = std::move(mpImage);

			MountFS(mpDiskInterface, *image, std::move(fs), mpImageName, mbWriteEnabled, mbAutoFlush);

			SetFocusToControl(IDC_DISK_CONTENTS);
		} catch(const MyError& e) {
			ShowError(VDTextAToW(e.gets()).c_str(), L"Disk load error");
			End(false);
			return true;
		}
	} else if (mpBlockDevice) {
		mFileNameView.SetText(mpImageName);
		mFileNameView.DeselectAll();
		mFileNameView.SetReadOnly(true);

		EnableControl(IDC_BROWSE, false);

		RefreshList();

		SetFocusToControl(IDC_DISK_CONTENTS);
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

	VDRegistryAppKey key("Settings", true);
	key.setBool("Disk Explorer: Strict filenames", mbStrictFilenames);
	key.setBool("Disk Explorer: Adjust filenames", mbAdjustFilenames);

	VDDialogFrameW32::OnDestroy();
}

bool ATUIDialogDiskExplorer::OnErase(VDZHDC hdc) {
	mResizer.Erase(&hdc);
	return true;
}

void ATUIDialogDiskExplorer::OnInitMenu(VDZHMENU hmenu) {
	VDCheckMenuItemByCommandW32(hmenu, ID_ADJUST_FILENAMES, mbAdjustFilenames);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_NAMECHECKING_STRICT, mbStrictFilenames);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_NAMECHECKING_RELAXED, !mbStrictFilenames);
}

bool ATUIDialogDiskExplorer::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_BROWSE || id == ID_FILE_OPENIMAGE) {
		if (mpImageName)
			return true;

		const VDStringW fn(VDGetLoadFileName(VDMAKEFOURCC('d', 'i', 's', 'k'),
			(VDGUIHandle)mhdlg,
			L"Choose disk image to browse",
			g_ATUIFileFilter_DiskWithArchives,
			NULL
			));

		if (!fn.empty()) {
			vdrefptr<IATDiskImage> image;
			vdautoptr<IATDiskFS> fs;

			// check if this image is already mounted on a disk
			ATDiskInterface *diskInterface = nullptr;
			vdrefptr<IATDiskImage> mountedImage;
			bool mountedImageWritable = true;
			bool mountedImageAutoFlush = true;

			const VDStringW& fnfull = VDGetFullPath(fn.c_str());
			for(int i=0; i<15; ++i) {
				ATDiskInterface& di = g_sim.GetDiskInterface(i);

				if (di.IsDiskBacked() && VDFileIsPathEqual(VDGetFullPath(di.GetPath()).c_str(), fnfull.c_str())) {
					IATDiskImage *dimage = di.GetDiskImage();
					
					if (dimage) {
						diskInterface = &di;
						mountedImage = dimage;
						mountedImageWritable = di.IsDiskWritable();
						mountedImageAutoFlush = (di.GetWriteMode() & kATMediaWriteMode_AutoFlush) != 0;
						break;
					}
				}
			}

			bool usingMountedImage = false;
			if (mountedImage) {
				ATUIGenericDialogOptions opts{};

				opts.mhParent = (VDGUIHandle)mhdlg;
				opts.mpTitle = L"Disk image already mounted";
				opts.mpMessage = L"This image is already mounted on a disk drive. Do you want to view the mounted image instead?\n\nThis ensures that the disk drive stays in sync with the image used by the disk explorer.";
				opts.mpIgnoreTag = "DiskExplorerMountActiveImage";
				opts.mAspectLimit = 4.0f;
				opts.mValidIgnoreMask = kATUIGenericResultMask_Yes | kATUIGenericResultMask_No;
				opts.mResultMask = kATUIGenericResultMask_YesNoCancel;
				opts.mIconType = kATUIGenericIconType_Warning;

				switch(ATUIShowGenericDialogAutoCenter(opts)) {
					case kATUIGenericResult_Yes:
						image = std::move(mountedImage);
						usingMountedImage = true;
						break;

					case kATUIGenericResult_No:
						break;

					case kATUIGenericResult_Cancel:
						return true;
				}
			}
				
			if (image) {
				fs = ATDiskMountImage(image, !image->IsUpdatable() || !mountedImageWritable);
			} else {
				const wchar_t *const fnp = fn.c_str();
				const bool isArc = !vdwcsicmp(VDFileSplitExt(fnp), L".arc");

				if (isArc) {
					fs = ATDiskMountImageARC(fnp);
				} else {
					ATImageLoadContext ctx;
					ctx.mLoadType = kATImageType_Disk;

					VDFileStream fstream(fnp);
					vdrefptr<IATImage> image0;
					ATImageLoadAuto(fnp, fnp, fstream, &ctx, nullptr, nullptr, ~image0);

					image = vdpoly_cast<IATDiskImage *>(image0);

					fs = ATDiskMountImage(image, !image->IsUpdatable());
				}
			}

			if (!fs)
				throw MyError("Unable to detect the file system on the disk image.");

			VDStringW displayedPath(fn);
			if (usingMountedImage)
				displayedPath += L" (mounted)";

			if (mountedImageWritable && !(VDFileGetAttributes(fn.c_str()) & kVDFileAttr_ReadOnly))
				mountedImageWritable = false;

			MountFS(diskInterface, *image, std::move(fs), displayedPath.c_str(), mountedImageWritable, mountedImageAutoFlush);
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
				FileListEntry *fle = GetFileListEntry(*it);

				if (fle && fle->mFileKey != ATDiskFSKey::None) {
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
	} else if (id == ID_DISKEXP_NEWFOLDER) {
		vdrefptr<FileListEntry> fle(new FileListEntry);

		fle->mFileName = L"New folder";
		fle->mFileKey = ATDiskFSKey::None;
		fle->mSectors = 0;
		fle->mBytes = 0;
		fle->mbIsDirectory = true;
		fle->mbIsCreate = true;
		fle->mbDateValid = false;

		int index = mList.InsertVirtualItem(-1, fle);
		mList.EnsureItemVisible(index);
		mList.EditItemLabel(index);

	} else if (id == ID_DISKEXP_VIEW) {
		vdfastvector<int> indices;
		mList.GetSelectedIndices(indices);

		if (!indices.empty()) {
			const int index = indices.front();
			FileListEntry *fle = GetFileListEntry(index);

			if (fle) {
				if (fle->mbIsDirectory) {
					OnItemDoubleClick(nullptr, index);
				} else if (fle->mFileKey != ATDiskFSKey::None) {
					vdfastvector<uint8> buf;

					try {
						mpFS->ReadFile(fle->mFileKey, buf);

						ATUIFileViewer viewer;
						viewer.SetBuffer(buf.data(), buf.size());
						viewer.ShowDialog(this);
					} catch(const MyError& e) {
						VDStringW str;
						str.sprintf(L"Cannot view file: %hs", e.gets());
						ShowError(str.c_str(), L"Altirra Error");
					}
				}
			}
		}
	} else if (id == ID_DISKEXP_IMPORTFILE || id == ID_DISKEXP_IMPORTTEXT) {
		const bool text = (id == ID_DISKEXP_IMPORTTEXT);
		const VDStringW& fn = VDGetLoadFileName('dexp', (VDGUIHandle)mhdlg, text ? L"Import text file" : L"Import binary file", L"All files (*.*)\0*.*\0", nullptr);

		if (!fn.empty()) {
			VDFile f(fn.c_str());

			uint64 len = (uint64)f.size();

			if (len >= 1 << 24)
				throw MyError("File is too large to import: %llu bytes", (unsigned long long)len);

			uint32 len2 = (uint32)len;
			vdblock<uint8> buf(len2);
			if (len2)
				f.read(buf.data(), len2);

			if (text) {
				auto begin = buf.begin();
				auto dst = begin;
				auto src = dst;
				auto end = buf.end();

				while(src != end) {
					uint8 c = *src++;

					if (c == 0x0D) {
						if (src != end && *src == 0x0A)
							++src;

						c = 0x9B;
					} else if (c == 0x0A)
						c = 0x9B;

					*dst++ = c;
				}

				len2 = (uint32)(dst - begin);
			}

			const VDDate creationTime = f.getCreationTime();
			f.closeNT();

			WriteFile(VDTextWToA(VDFileSplitPathRightSpan(fn)).c_str(), buf.data(), len2, creationTime);

			OnFSModified();
		}
	} else if (id == ID_DISKEXP_EXPORTFILE || id == ID_DISKEXP_EXPORTTEXT) {
		const bool text = (id == ID_DISKEXP_EXPORTTEXT);
		vdfastvector<int> indices;
		mList.GetSelectedIndices(indices);

		if (!indices.empty()) {
			const int index = indices.front();
			FileListEntry *fle = GetFileListEntry(index);

			if (fle && !fle->mbIsDirectory) {
				vdfastvector<uint8> buf;
				mpFS->ReadFile(fle->mFileKey, buf);

				ATDiskFSEntryInfo fileInfo;
				mpFS->GetFileInfo(fle->mFileKey, fileInfo);

				if (text) {
					const size_t numEOLs = std::count(buf.begin(), buf.end(), (uint8)0x9B);

					if (numEOLs) {
						const size_t origSize = buf.size();

						buf.resize(origSize + numEOLs);

						auto begin = buf.begin();
						auto src = begin + origSize;
						auto dst = buf.end();

						while(dst != begin) {
							uint8 c = *--src;

							if (c == 0x9B) {
								*--dst = 0x0A;
								c = 0x0D;
							}

							*--dst = c;
						}
					}
				}

				VDSetLastLoadSaveFileName('dexp', fle->mFileName.c_str());
				const VDStringW& fn = VDGetSaveFileName('dexp', (VDGUIHandle)mhdlg, text ? L"Export text file" : L"Export binary file", L"All files (*.*)\0*.*\0", nullptr);

				if (!fn.empty()) {
					VDFile f(fn.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);

					f.write(buf.data(), (long)buf.size());

					if (fle->mbDateValid)
						f.setCreationTime(VDDateFromLocalDate(fle->mDate));
				}
			}
		}
	} else if (id == ID_PARTITION_OPEN) {
		PartitionListEntry *ple = GetPartitionListEntry(mList.GetSelectedIndex());

		if (ple)
			OpenPartition(*ple);
	} else if (id == ID_PARTITION_IMPORT) {
		PartitionListEntry *ple = GetPartitionListEntry(mList.GetSelectedIndex());
		if (!ple)
			return true;

		vdrefptr<ATPartitionDiskView> pdview { new ATPartitionDiskView(*mpBlockDevice, *ple) };
		if (!pdview->IsUpdatable())
			throw MyError("Cannot import disk image as partition is read-only.");

		const VDStringW& s = VDGetLoadFileName('disk', (VDGUIHandle)mhdlg, L"Import disk image to partition",
			g_ATUIFileFilter_Disk, nullptr);

		if (!s.empty()) {
			vdrefptr<IATDiskImage> image;

			{
				VDFileStream fstream(s.c_str());
				ATImageLoadContext ctx;
				ctx.mLoadType = kATImageType_Disk;

				vdrefptr<IATImage> image0;
				ATImageLoadAuto(s.c_str(), s.c_str(), fstream, &ctx, nullptr, nullptr, ~image0);

				image = vdpoly_cast<IATDiskImage *>(image0);
			}

			const uint32 partSectorCount = pdview->GetVirtualSectorCount();
			const uint32 partSectorSize = pdview->GetSectorSize();
			const uint32 imageSectorCount = image->GetVirtualSectorCount();
			const uint32 imageSectorSize = image->GetSectorSize();

			if (imageSectorCount != partSectorCount ||
				imageSectorSize != partSectorSize)
			{
				VDStringW msg;
				msg.sprintf(
					L"Partition and image geometries do not match.\n"
					L"\n"
					L"Partition: %u sectors of %u bytes\n"
					L"Image: %u sectors of %u bytes",
					partSectorCount,
					partSectorSize,
					imageSectorCount,
					imageSectorSize);

				ShowError2(msg.c_str(), L"Cannot import disk image");
				return true;
			}

			uint8 secbuf[512];

			for(uint32 i=0; i<partSectorCount; ++i) {
				uint32 len = pdview->GetSectorSize(i);
				uint32 actual = image->ReadVirtualSector(i, secbuf, len);

				if (actual < len)
					memset(&secbuf[actual], 0, len - actual);

				pdview->WriteVirtualSector(i, secbuf, len);
			}
		}

		return true;
	} else if (id == ID_PARTITION_EXPORT) {
		PartitionListEntry *ple = GetPartitionListEntry(mList.GetSelectedIndex());

		if (ple) {
			VDStringW s(VDGetSaveFileName(
					'disk',
					(VDGUIHandle)mhdlg,
					L"Export partition to disk image",
					L"Atari disk image (*.atr)\0*.atr\0"
						L"All files\0*.*\0",
					L"atr"));

			if (!s.empty()) {
				vdrefptr<ATPartitionDiskView> pdview { new ATPartitionDiskView(*mpBlockDevice, *ple) };
				vdrefptr<IATDiskImage> newImage;

				ATCreateDiskImage(pdview->GetGeometry(), ~newImage);

				uint8 secbuf[512];
				const uint32 n = pdview->GetVirtualSectorCount();

				for(uint32 i=0; i<n; ++i) {
					const uint32 sz = pdview->GetSectorSize(i);

					pdview->ReadVirtualSector(i, secbuf, sz);
					newImage->WriteVirtualSector(i, secbuf, sz);
				}

				newImage->Save(s.c_str(), kATDiskImageFormat_ATR);
			}
		}

		return true;
	} else if (id == ID_NAMECHECKING_STRICT) {
		mbStrictFilenames = true;

		if (mpFS)
			mpFS->SetStrictNameChecking(mbStrictFilenames);
	} else if (id == ID_NAMECHECKING_RELAXED) {
		mbStrictFilenames = false;

		if (mpFS)
			mpFS->SetStrictNameChecking(mbStrictFilenames);
	} else if (id == ID_ADJUST_FILENAMES) {
		mbAdjustFilenames = !mbAdjustFilenames;
		return true;
	}

	return false;
}

void ATUIDialogDiskExplorer::OnItemBeginDrag(VDUIProxyListView *sender, int item) {
	vdrefptr<IATUIDiskExplorerDataObjectW32> dataObject;
	ATUICreateDiskExplorerDataObjectW32(mpFS, ~dataObject);

	vdfastvector<int> indices;
	mList.GetSelectedIndices(indices);

	for(vdfastvector<int>::const_iterator it(indices.begin()), itEnd(indices.end());
		it != itEnd;
		++it)
	{
		FileListEntry *fle = GetFileListEntry(*it);

		if (fle && fle->mFileKey != ATDiskFSKey::None)
			dataObject->AddFile(fle->mFileKey, fle->mBytes, fle->mbDateValid ? &fle->mDate : nullptr, fle->mFileName.c_str());
	}

	DWORD srcEffects = 0;

	SHSTOCKICONINFO ssii = {sizeof(SHSTOCKICONINFO)};

	SHGetStockIconInfo(SIID_DOCNOASSOC, SHGSI_ICON | SHGSI_LARGEICON, &ssii);

	if (ssii.hIcon) {
		ICONINFO ii {};
		BITMAP bm {};
		if (GetIconInfo(ssii.hIcon, &ii)) {
			if (GetObject(ii.hbmColor, sizeof(BITMAP), &bm)) {
				vdrefptr<IDragSourceHelper> dragSourceHelper;
				HRESULT hr = CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC, IID_IDragSourceHelper, (void **)~dragSourceHelper);

				if (SUCCEEDED(hr)) {
					vdrefptr<IDragSourceHelper2> dragSourceHelper2;

					hr = dragSourceHelper->QueryInterface(IID_IDragSourceHelper2, (void **)~dragSourceHelper2);
					if (SUCCEEDED(hr))
						dragSourceHelper2->SetFlags(DSH_ALLOWDROPDESCRIPTIONTEXT);

					SHDRAGIMAGE shdi {};
					shdi.sizeDragImage = SIZE { bm.bmWidth, bm.bmHeight };
					shdi.ptOffset = POINT { (LONG)ii.xHotspot, (LONG)ii.yHotspot };
					shdi.hbmpDragImage = ii.hbmColor;
					dragSourceHelper->InitializeFromBitmap(&shdi, dataObject->AsDataObject());
				}
			}

			if (ii.hbmColor)
				DeleteObject(ii.hbmColor);

			if (ii.hbmMask)
				DeleteObject(ii.hbmMask);
		}

		DestroyIcon(ssii.hIcon);
	}

	SHDoDragDrop(nullptr, dataObject->AsDataObject(), nullptr, DROPEFFECT_COPY, &srcEffects);
}

void ATUIDialogDiskExplorer::OnItemContextMenu(VDUIProxyListView *sender, VDUIProxyListView::ContextMenuEvent& event) {
	if (!mhMenuItemContext)
		return;

	if (mpFS) {
		HMENU hmenu = GetSubMenu(mhMenuItemContext, 0);

		const int idx = mList.GetSelectedIndex();
		const bool writable = !mpFS->IsReadOnly();
		VDEnableMenuItemByCommandW32(hmenu, ID_DISKEXP_VIEW, idx >= 0);
		VDEnableMenuItemByCommandW32(hmenu, ID_DISKEXP_RENAME, idx >= 0 && writable);
		VDEnableMenuItemByCommandW32(hmenu, ID_DISKEXP_DELETE, idx >= 0 && writable);
		VDEnableMenuItemByCommandW32(hmenu, ID_DISKEXP_NEWFOLDER, writable);
		VDEnableMenuItemByCommandW32(hmenu, ID_DISKEXP_IMPORTFILE, writable);
		VDEnableMenuItemByCommandW32(hmenu, ID_DISKEXP_IMPORTTEXT, writable);

		bool fileSelected = false;

		if (idx >= 0) {
			FileListEntry *fle = GetFileListEntry(idx);

			if (fle && !fle->mbIsDirectory)
				fileSelected = true;
		}

		VDEnableMenuItemByCommandW32(hmenu, ID_DISKEXP_EXPORTFILE, fileSelected);
		VDEnableMenuItemByCommandW32(hmenu, ID_DISKEXP_EXPORTTEXT, fileSelected);

		TrackPopupMenu(hmenu, TPM_LEFTALIGN | TPM_TOPALIGN, event.mX, event.mY, 0, mhdlg, NULL);
	} else if (mpBlockDevice) {
		PartitionListEntry *ple = GetPartitionListEntry(mList.GetSelectedIndex());

		if (ple) {
			HMENU hmenu = GetSubMenu(mhMenuItemContext, 1);

			const bool writable = !mpBlockDevice->IsReadOnly() && !ple->mbWriteProtected;
			VDEnableMenuItemByCommandW32(hmenu, ID_PARTITION_IMPORT, writable);

			TrackPopupMenu(hmenu, TPM_LEFTALIGN | TPM_TOPALIGN, event.mX, event.mY, 0, mhdlg, NULL);
		}
	} 
}

void ATUIDialogDiskExplorer::OnItemLabelChanged(VDUIProxyListView *sender, VDUIProxyListView::LabelChangedEvent *event) {
	int idx = event->mIndex;

	FileListEntry *fle = GetFileListEntry(idx);
	if (!fle)
		return;

	// check if we were creating a new directory
	if (fle->mbIsCreate) {
		// check if it was cancelled
		if (!event->mpNewLabel) {
			mList.DeleteItem(idx);
		} else {
			// try creating the dir
			//
			// note that we can't modify the list in the callback, as that causes
			// the list view control to blow up
			try {
				mpFS->CreateDir(mCurrentDirKey, VDTextWToA(event->mpNewLabel).c_str());

				PostCall([this]() { OnFSModified(); });
			} catch(const MyError& e) {
				event->mbAllowEdit = false;

				VDStringW s;
				s.sprintf(L"Cannot create directory \"%ls\": %hs", event->mpNewLabel, e.gets());
				ShowError(s.c_str(), L"Altirra Error");

				PostCall([idx,this]() { mList.DeleteItem(idx); });
			}
		}

		return;
	}

	if (fle->mFileKey == ATDiskFSKey::None || !event->mpNewLabel)
		return;

	try {
		mpFS->RenameFile(fle->mFileKey, VDTextWToA(event->mpNewLabel).c_str());

		PostCall([this]() { OnFSModified(); });
	} catch(const MyError& e) {
		event->mbAllowEdit = false;

		VDStringW s;
		s.sprintf(L"Cannot rename \"%ls\" to \"%ls\": %hs", fle->mFileName.c_str(), event->mpNewLabel, e.gets());
		ShowError(s.c_str(), L"Altirra Error");
	}
}

void ATUIDialogDiskExplorer::OnItemDoubleClick(VDUIProxyListView *sender, int item) {
	ListEntry *le = GetListEntry(item);
	if (!le)
		return;
	
	try {
		if (FileListEntry *fle = vdpoly_cast<FileListEntry *>(le)) {
			if (!fle->mbIsDirectory)
				return;

			if (fle->mFileKey != ATDiskFSKey::None)
				mCurrentDirKey = fle->mFileKey;
			else if (mCurrentDirKey != ATDiskFSKey::None)
				mCurrentDirKey = mpFS->GetParentDirectory(mCurrentDirKey);
			else if (mpBlockDevice) {
				mpDropTarget->SetFS(nullptr);
				mpFS = nullptr;
				mpImage = nullptr;
				mFileNameView.SetText(mpImageName);
			}

			RefreshList();
		} else if (PartitionListEntry *ple = vdpoly_cast<PartitionListEntry *>(le)) {
			OpenPartition(*ple);
		}
	} catch(const MyError& e) {
		ShowError(e);
	}
}

void ATUIDialogDiskExplorer::OpenPartition(const PartitionListEntry& ple) {
	vdrefptr<ATPartitionDiskView> pdview { new ATPartitionDiskView(*mpBlockDevice, ple) };
	vdautoptr<IATDiskFS> fs(ATDiskMountImage(pdview, !pdview->IsUpdatable()));

	if (!fs)
		throw MyError("Unable to detect the file system on the disk image.");

	MountFS(nullptr, *pdview, std::move(fs), ple.mName.c_str(), false, false);
}

ATUIDialogDiskExplorer::ListEntry *ATUIDialogDiskExplorer::GetListEntry(int idx) {
	if (idx < 0)
		return nullptr;

	return static_cast<ListEntry *>(mList.GetVirtualItem(idx));
}

ATUIDialogDiskExplorer::FileListEntry *ATUIDialogDiskExplorer::GetFileListEntry(int idx) {
	return vdpoly_cast<FileListEntry *>(GetListEntry(idx));
}

ATUIDialogDiskExplorer::PartitionListEntry *ATUIDialogDiskExplorer::GetPartitionListEntry(int idx) {
	return vdpoly_cast<PartitionListEntry *>(GetListEntry(idx));
}

void ATUIDialogDiskExplorer::OnFSModified() {
	try {
		mpFS->Flush();

		if (mbAutoFlush && mpImage)
			mpImage->Flush();
	} catch(const MyError& e) {
		ShowError(e);
	}

	if (mpDiskInterface)
		mpDiskInterface->OnDiskChanged(true);

	RefreshList();
}

void ATUIDialogDiskExplorer::MountFS(ATDiskInterface *diskInterface, IATDiskImage& image, vdautoptr<IATDiskFS> fs, const wchar_t *fsDescription, bool write, bool autoFlush) {
	mpDropTarget->SetFS(nullptr);

	mpImage = &image;
	mpFS = std::move(fs);

	mpFS->SetStrictNameChecking(mbStrictFilenames);

	mbWriteEnabled = write;
	mbAutoFlush = autoFlush;
	mpDiskInterface = diskInterface;

	mpDropTarget->SetFS(mpFS);

	mCurrentDirKey = ATDiskFSKey::None;

	mFileNameView.SetText(fsDescription);

	if (write) {
		if (mpImage && mpImage->IsUpdatable() && mpFS->IsReadOnly()) {
			ShowWarning(L"This disk format is only supported in read-only mode.", L"Altirra Warning");
		} else {
			ValidateForWrites();
		}
	}

	// must be after above to show read only state properly in UI
	RefreshList();
}

void ATUIDialogDiskExplorer::ValidateForWrites() {
	ATDiskFSValidationReport validationReport;
	if (!mpFS->Validate(validationReport)) {
		// Check if we had a serious error for which we should block writes.
		if (!validationReport.IsSerious() && validationReport.mbBitmapIncorrectLostSectorsOnly) {
			ShowWarning(L"The allocation bitmap on this disk is incorrect: some sectors are marked allocated when they are actually free.");
		} else {
			mpFS->SetReadOnly(true);

			ShowWarning(
				validationReport.mbBrokenFiles || validationReport.mbOpenWriteFiles
					? L"The file system on this disk is damaged and has been mounted as read-only to prevent further damage."
					: L"The allocation bitmap on this disk is incorrect. The disk has been mounted read-only as a precaution to prevent further damage.",
				L"Altirra Warning");
		}
	}
}

void ATUIDialogDiskExplorer::RefreshList() {
	mList.SetRedraw(false);
	mList.Clear();

	if (mpFS)
		RefreshListFromFileSystem();
	else if (mpBlockDevice)
		RefreshListFromPartitions();

	mList.AutoSizeColumns();
	mList.SetRedraw(true);
}

void ATUIDialogDiskExplorer::RefreshListFromPartitions() {
	vdvector<ATPartitionInfo> partitions;
	ATDecodePartitionTable(*mpBlockDevice, partitions);

	for(const ATPartitionInfo& pi : partitions) {
		vdrefptr<PartitionListEntry> fle(new PartitionListEntry);

		fle->InitFrom(pi);

		int item = mList.InsertVirtualItem(-1, fle);
		if (item >= 0)
			mList.SetItemImage(item, mIconPartition);
	}

	SetControlText(IDC_STATUS, L"Mounted block device.");
}

void ATUIDialogDiskExplorer::RefreshListFromFileSystem() {
	if (mCurrentDirKey != ATDiskFSKey::None || mpBlockDevice) {
		vdrefptr<FileListEntry> fle(new FileListEntry);

		fle->mFileName = L"..";
		fle->mFileKey = ATDiskFSKey::None;
		fle->mbIsDirectory = true;
		fle->mbIsCreate = false;

		int item = mList.InsertVirtualItem(-1, fle);
		if (item >= 0)
			mList.SetItemImage(item, mIconFolder);
	}

	// Read directory
	ATDiskFSEntryInfo einfo;

	ATDiskFSFindHandle searchKey = mpFS->FindFirst(mCurrentDirKey, einfo);

	vdfastvector<FileListEntry *> fles;

	try {
		if (searchKey != ATDiskFSFindHandle::Invalid) {
			do {
				vdrefptr<FileListEntry> fle(new FileListEntry);

				fle->InitFrom(einfo);

				fles.emplace_back();
				fles.back() = fle.release();
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

	RefreshFsInfo();
}

void ATUIDialogDiskExplorer::RefreshFsInfo() {
	ATDiskFSInfo fsinfo;
	mpFS->GetInfo(fsinfo);

	VDStringW s;
	s.sprintf(L"Mounted %hs file system%hs. %d block%s (%uKB) free"
		, fsinfo.mFSType.c_str(), mpFS->IsReadOnly() ? " (read-only)" : ""
		, fsinfo.mFreeBlocks
		, fsinfo.mFreeBlocks != 1 ? L"s" : L""
		, (fsinfo.mFreeBlocks * fsinfo.mBlockSize) >> 10
		);

	SetControlText(IDC_STATUS, s.c_str());
}

void ATUIDialogDiskExplorer::WriteFile(const char *filename, const void *data, uint32 len, const VDDate& creationTime) {
	const bool requireFirstAlpha = mbStrictFilenames;
	char fnbuf[13];
	int pass = 0;
	int nameLen = 0;

	for(;;) {
		try {
			const auto fileKey = mpFS->WriteFile(GetDropTargetParentKey(), filename, data, len);

			if (creationTime != VDDate{})
				mpFS->SetFileTimestamp(fileKey, VDGetLocalDate(creationTime));
		} catch(const ATDiskFSException& e) {
			if (!mbAdjustFilenames || (e.GetErrorCode() != kATDiskFSError_InvalidFileName
				&& e.GetErrorCode() != kATDiskFSError_FileExists))
				throw;

			if (++pass >= 100)
				throw;

			// For a first try, just strip out all non-conformant characters.
			if (pass == 1) {
				int sectionLen = 0;
				int sectionLimit = 8;
				bool inExt = false;
				char *dst = fnbuf;

				for(const char *s = filename; *s; ++s) {
					char c = *s;

					if (c == '.') {
						if (inExt)
							break;

						inExt = true;
						nameLen = sectionLen;
						sectionLen = 0;
						sectionLimit = 3;
						*dst++ = '.';
					} else if (sectionLen < sectionLimit) {
						if (c >= 'a' && c <= 'z')
							c &= 0xDF;

						if (c >= '0' && c <= '9') {
							if (!inExt && !sectionLen && requireFirstAlpha) {
								*dst++ = 'X';
								++sectionLen;
							}
						} else if (c < 'A' || c > 'Z') {
							if (mbStrictFilenames || c != '@' && c != '_')
								continue;
						}

						*dst++ = c;
						++sectionLen;
					}
				}

				if (!inExt)
					nameLen = sectionLen;

				*dst = 0;

				filename = fnbuf;
			} else {
				// increment number on filename
				int pos = nameLen - 1;
				bool incSucceeded = false;
				while(pos >= 0) {
					char c = fnbuf[pos];

					if (c >= '0' && c <= '8') {
						++fnbuf[pos];
						incSucceeded = true;
						break;
					} else if (c == '9') {
						fnbuf[pos] = '0';
						--pos;
					} else
						break;
				}

				if (incSucceeded)
					continue;

				// no more room to increment current number -- try to add
				// another digit
				if (nameLen >= 8) {
					// no more room in the filename -- try to steal another char
					if (pos < 4)
						throw;

					fnbuf[pos] = '1';
					continue;
				}

				// shift in a new digit after the current stopping position (which
				// may be -1 if we're at the start).
				if (pos < 0 && requireFirstAlpha) {
					memmove(fnbuf + 1, fnbuf, sizeof(fnbuf[0]) * (vdcountof(fnbuf) - 1));

					fnbuf[0] = 'X';
				}

				memmove(fnbuf + pos + 2, fnbuf + pos + 1, sizeof(fnbuf[0]) * (vdcountof(fnbuf) - (pos + 2)));
				fnbuf[pos + 1] = '1';
				++nameLen;
			}
			continue;
		}

		break;
	}
}

LRESULT ATUIDialogDiskExplorer::ListViewSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_MOUSEACTIVATE)
		return MA_NOACTIVATE;

	LRESULT lr = CallWindowProc(mListViewWndProc, hwnd, msg, wParam, lParam);

	if (msg == WM_GETDLGCODE && lParam) {
		const MSG& inputMsg = *(const MSG *)lParam;

		if (inputMsg.message == WM_KEYDOWN || inputMsg.message == WM_KEYUP) {
			if (inputMsg.wParam == VK_RETURN)
				lr |= DLGC_WANTMESSAGE;
		}
	}

	return lr;
}

///////////////////////////////////////////////////////////////////////////

void ATUIShowDialogDiskExplorer(VDGUIHandle h) {
	ATUIDialogDiskExplorer dlg;

	dlg.ShowDialog(h);
}

void ATUIShowDialogDiskExplorer(VDGUIHandle h, IATDiskImage *image, const wchar_t *imageName, bool writeEnabled, bool autoFlush, ATDiskInterface *di) {
	ATUIDialogDiskExplorer dlg(image, imageName, writeEnabled, autoFlush, di);

	dlg.ShowDialog(h);
}

void ATUIShowDialogDiskExplorer(VDGUIHandle h, IATBlockDevice *dev, const wchar_t *devName) {
	ATUIDialogDiskExplorer dlg(nullptr, devName, !dev->IsReadOnly(), true, nullptr, dev);

	dlg.ShowDialog(h);
}
