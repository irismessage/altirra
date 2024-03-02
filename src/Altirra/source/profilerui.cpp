//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2010 Avery Lee
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
#include <vd2/system/w32assist.h>
#include <windows.h>
#include <commctrl.h>
#include <tchar.h>
#include "uiframe.h"
#include "uiproxies.h"
#include "console.h"
#include "resource.h"
#include "simulator.h"
#include "profiler.h"
#include "debugger.h"
#include "disasm.h"

extern ATSimulator g_sim;

/////////////////////////////////////////////////////////////////////////////

class ATUIProfilerSourceTextView : public VDShaderEditorBaseWindow {
public:
	ATUIProfilerSourceTextView(const ATProfileSession& profsess);
	~ATUIProfilerSourceTextView();

	bool Create(HWND hwndParent);
	void Destroy();

	void SetTargetAddress(uint32 addr);
	void SetColumnWidths(int widths[3]);

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	void OnCreate();
	void OnSize();
	void OnPaint();
	void OnVScroll(int code);
	void RemakeView();

	typedef vdfastvector<ATProfileRecord> Records;
	Records		mRecords;

	uint32		mTargetAddress;
	uint32		mLineHeight;
	uint32		mLinesVisible;
	HFONT		mFont;
	VDStringA	mBuffer;

	int			mColumnWidths[3];

	typedef vdfastvector<uint32> Lines;
	Lines mLines;

	struct RecordSort {
		bool operator()(const ATProfileRecord& x, const ATProfileRecord& y) const {
			return x.mAddress < y.mAddress;
		}

		bool operator()(const ATProfileRecord& x, uint32 addr) const {
			return x.mAddress < addr;
		}

		bool operator()(uint32 addr, const ATProfileRecord& y) const {
			return addr < y.mAddress;
		}
	};
};

ATUIProfilerSourceTextView::ATUIProfilerSourceTextView(const ATProfileSession& profsess)
	: mRecords(profsess.mRecords)
	, mTargetAddress(0)
{
	for(Records::iterator it(mRecords.begin()), itEnd(mRecords.end()); it != itEnd; ++it) {
		ATProfileRecord& r = *it;

		r.mAddress &= 0xffff;
	}

	// sort records
	std::sort(mRecords.begin(), mRecords.end(), RecordSort());

	// collapse records that now have the same address
	Records::iterator itDst(mRecords.begin());
	for(Records::iterator it(mRecords.begin()), itEnd(mRecords.end()); it != itEnd; ) {
		uint32 addr = it->mAddress;

		ATProfileRecord& dr = *itDst;
		++itDst;
		
		dr = *it;
		while(++it != itEnd) {
			if (it->mAddress != addr)
				break;

			dr.mCalls += it->mCalls;
			dr.mCycles += it->mCycles;
			dr.mInsns += it->mInsns;
		}
	}

	mRecords.erase(itDst, mRecords.end());
}

ATUIProfilerSourceTextView::~ATUIProfilerSourceTextView() {
}

bool ATUIProfilerSourceTextView::Create(HWND hwndParent) {
	if (!CreateWindow(MAKEINTATOM(sWndClass), _T("Altirra Profiler - Source View"), WS_VISIBLE|WS_CHILD, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndParent, NULL, VDGetLocalModuleHandleW32(), static_cast<VDShaderEditorBaseWindow *>(this)))
		return false;

	return true;
}

void ATUIProfilerSourceTextView::Destroy() {
	if (mhwnd)
		DestroyWindow(mhwnd);
}

void ATUIProfilerSourceTextView::SetTargetAddress(uint32 addr) {
	addr &= 0xffff;

	if (mTargetAddress == addr)
		return;

	mTargetAddress = addr;

	SCROLLINFO si = {sizeof(SCROLLINFO)};
	si.fMask = SIF_POS;
	si.nPos = addr;
	SetScrollInfo(mhwnd, SB_VERT, &si, TRUE);

	RemakeView();
}

void ATUIProfilerSourceTextView::SetColumnWidths(int widths[2]) {
	mColumnWidths[0] = widths[0];
	mColumnWidths[1] = widths[0] + widths[1];
	mColumnWidths[2] = mColumnWidths[1] + widths[2];

	if (mhwnd)
		InvalidateRect(mhwnd, NULL, TRUE);
}

LRESULT ATUIProfilerSourceTextView::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			OnCreate();
			break;

		case WM_SIZE:
			OnSize();
			break;

		case WM_PAINT:
			OnPaint();
			return 0;

		case WM_VSCROLL:
			OnVScroll(LOWORD(wParam));
			return 0;

		case WM_KEYDOWN:
			switch(LOWORD(wParam)) {
				case VK_UP:
					OnVScroll(SB_LINEUP);
					return 0;

				case VK_DOWN:
					OnVScroll(SB_LINEDOWN);
					return 0;

				case VK_PRIOR:
					OnVScroll(SB_PAGEUP);
					return 0;

				case VK_NEXT:
					OnVScroll(SB_PAGEDOWN);
					return 0;
			}
			break;
	}

	return DefWindowProc(mhwnd, msg, wParam, lParam);
}

void ATUIProfilerSourceTextView::OnCreate() {
	mFont = (HFONT)ATGetConsoleFontW32();
	mLineHeight = ATGetConsoleFontLineHeightW32();

	ShowScrollBar(mhwnd, SB_VERT, TRUE);

	SCROLLINFO si;
    si.cbSize		= sizeof(SCROLLINFO);
	si.fMask		= SIF_POS | SIF_PAGE | SIF_RANGE;
    si.nMin			= 0;
    si.nMax			= 0xFFFF;
    si.nPage		= 1;
	si.nPos			= mTargetAddress;
    si.nTrackPos	= 0;
	SetScrollInfo(mhwnd, SB_VERT, &si, TRUE);
}

void ATUIProfilerSourceTextView::OnSize() {
	RECT r = {0};
	GetClientRect(mhwnd, &r);

	uint32 linesVisible = (r.bottom + mLineHeight - 1) / (int)mLineHeight;

	if (!linesVisible)
		linesVisible = 1;

	if (linesVisible != mLinesVisible) {
		mLinesVisible = linesVisible;
		RemakeView();
	}
}

void ATUIProfilerSourceTextView::OnPaint() {
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(mhwnd, &ps);
	if (!hdc)
		return;

	SelectObject(hdc, mFont);
	SetTextAlign(hdc, TA_LEFT | TA_TOP);
	SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
	SetBkMode(hdc, TRANSPARENT);

	Lines::const_iterator itLines(mLines.begin()), itLinesEnd(mLines.end());

	int y1 = 0;
	while(y1 < ps.rcPaint.bottom && itLines != itLinesEnd) {
		const int y2 = y1 + mLineHeight;
		if (y2 > ps.rcPaint.top) {
			uint32 addr = *itLines;

			// render counts
			Records::const_iterator it(std::lower_bound(mRecords.begin(), mRecords.end(), addr, RecordSort()));

			if (it != mRecords.end() && it->mAddress == addr) {
				const ATProfileRecord& r = *it;

				mBuffer.sprintf(" %u", r.mCycles);
				RECT rc1 = {0, y1, mColumnWidths[0], y2};
				ExtTextOutA(hdc, rc1.left, y1, ETO_CLIPPED, &rc1, mBuffer.data(), mBuffer.size(), NULL);

				mBuffer.sprintf(" %u", r.mInsns);
				RECT rc2 = {mColumnWidths[0], y1, mColumnWidths[1], y2};
				ExtTextOutA(hdc, rc2.left, y1, ETO_CLIPPED, &rc2, mBuffer.data(), mBuffer.size(), NULL);

				mBuffer.sprintf(" %.1f", r.mInsns ? (float)r.mCycles / (float)r.mInsns : 0.0f);
				RECT rc3 = {mColumnWidths[1], y1, mColumnWidths[2], y2};
				ExtTextOutA(hdc, rc3.left, y1, ETO_CLIPPED, &rc3, mBuffer.data(), mBuffer.size(), NULL);
			}

			// render line
			mBuffer = " ";
			ATDisassembleInsn(mBuffer, addr, false);

			ExtTextOutA(hdc, mColumnWidths[2], y1, 0, NULL, mBuffer.data(), mBuffer.size(), NULL);
		}

		y1 += mLineHeight;
		++itLines;
	}

	EndPaint(mhwnd, &ps);
}

void ATUIProfilerSourceTextView::OnVScroll(int code) {
	SCROLLINFO si={sizeof(SCROLLINFO)};
	si.fMask = SIF_POS | SIF_TRACKPOS | SIF_RANGE;
	if (!GetScrollInfo(mhwnd, SB_VERT, &si))
		return;

	int newPos = si.nPos;

	switch(code) {
		case SB_TOP:
			newPos = 0;
			break;
		case SB_BOTTOM:
			newPos = si.nMax - si.nPage;
			break;
		case SB_LINEUP:
			newPos = mLines[(mLinesVisible >> 1) - 1];
			break;
		case SB_LINEDOWN:
			newPos = mLines[(mLinesVisible >> 1) + 1];
			break;
		case SB_PAGEUP:
			newPos = mLines.front();
			break;
		case SB_PAGEDOWN:
			newPos = mLines.back();
			break;
		case SB_THUMBPOSITION:
			newPos = si.nTrackPos;
			break;
		case SB_THUMBTRACK:
			newPos = si.nTrackPos;
			break;
	}

	if (newPos > (int)(si.nMax - si.nPage))
		newPos = si.nMax - si.nPage;
	else if (newPos < 0)
		newPos = 0;

	if (newPos != si.nPos) {
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_POS;
		si.nPos = newPos;
		SetScrollInfo(mhwnd, SB_VERT, &si, TRUE);
		mTargetAddress = newPos;
		RemakeView();
	}
}

void ATUIProfilerSourceTextView::RemakeView() {
	uint32 linesAbove = mLinesVisible >> 1;
	uint32 nextAddr = mTargetAddress - linesAbove*3;
	uint32 stepAddr = nextAddr;

	mLines.clear();

	// compute lines prior to target address
	while(stepAddr != mTargetAddress) {
		if (nextAddr != stepAddr) {
			Records::const_iterator it(std::lower_bound(mRecords.begin(), mRecords.end(), stepAddr, RecordSort()));

			if (it == mRecords.end() || it->mAddress != stepAddr) {
				++stepAddr;
				stepAddr &= 0xffff;
				continue;
			}
		}

		nextAddr += ATGetOpcodeLength(g_sim.DebugReadByte(nextAddr));

		mLines.push_back(stepAddr);

		++stepAddr;
		stepAddr &= 0xffff;
	}

	// trim off lines if we have too many
	size_t n = mLines.size();

	if (n > linesAbove) {
		mLines.erase(mLines.begin(), mLines.begin() + (n - linesAbove));
		n = linesAbove;
	}

	// fill out remaining lines
	while(n < mLinesVisible) {
		if (nextAddr != stepAddr) {
			Records::const_iterator it(std::lower_bound(mRecords.begin(), mRecords.end(), stepAddr, RecordSort()));

			if (it == mRecords.end() || it->mAddress != stepAddr) {
				++stepAddr;
				stepAddr &= 0xffff;
				continue;
			}
		}

		nextAddr += ATGetOpcodeLength(g_sim.DebugReadByte(nextAddr));

		mLines.push_back(stepAddr);
		++n;
		++stepAddr;
		stepAddr &= 0xffff;
	}

	if (mhwnd)
		InvalidateRect(mhwnd, NULL, TRUE);
}

/////////////////////////////////////////////////////////////////////////////

class ATUIProfilerSourcePane : public VDShaderEditorBaseWindow {
public:
	ATUIProfilerSourcePane(const ATProfileSession& session, uint32 baseAddr);
	~ATUIProfilerSourcePane();

	bool Create(HWND hwndParent);

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	void OnCreate();
	void OnDestroy();
	void OnSize();
	void UpdateViewColumnWidths();

	uint32 mBaseAddress;
	HWND mhwndHeader;
	vdrefptr<ATUIProfilerSourceTextView> mpSourceView;

	ATProfileSession mSession;
};

ATUIProfilerSourcePane::ATUIProfilerSourcePane(const ATProfileSession& session, uint32 baseAddr)
	: mhwndHeader(NULL)
	, mBaseAddress(baseAddr)
	, mSession(session)
{
}

ATUIProfilerSourcePane::~ATUIProfilerSourcePane() {
}

bool ATUIProfilerSourcePane::Create(HWND hwndParent) {
	if (!CreateWindow(MAKEINTATOM(sWndClass), _T("Altirra Profiler - Source View"), WS_VISIBLE|WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndParent, NULL, VDGetLocalModuleHandleW32(), static_cast<VDShaderEditorBaseWindow *>(this)))
		return false;

	return true;
}

LRESULT ATUIProfilerSourcePane::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			OnCreate();
			break;

		case WM_DESTROY:
			OnDestroy();
			break;

		case WM_SIZE:
			OnSize();
			break;

		case WM_NOTIFY:
			{
				const NMHDR& hdr = *(const NMHDR *)lParam;

				if (hdr.hwndFrom == mhwndHeader) {
					if (hdr.code == HDN_ITEMCHANGED) {
						const NMHEADER& hdr2 = reinterpret_cast<const NMHEADER&>(hdr);

						if ((hdr2.pitem->mask & HDI_WIDTH) && hdr2.iItem < 3)
							UpdateViewColumnWidths();
					}
				}
			}
			break;

		case WM_SETFOCUS:
			if (HWND hwndView = mpSourceView->GetHandleW32())
				::SetFocus(hwndView);
			return TRUE;
	}

	return VDShaderEditorBaseWindow::WndProc(msg, wParam, lParam);
}

void ATUIProfilerSourcePane::OnCreate() {
	mhwndHeader = CreateWindow(WC_HEADER, _T(""), WS_CHILD | WS_VISIBLE | HDS_HORZ | HDS_BUTTONS | HDS_FULLDRAG, 0, 0, 0, 0, mhwnd, (HMENU)100, VDGetLocalModuleHandleW32(), NULL);

	SendMessage(mhwndHeader, WM_SETFONT, (WPARAM)(HFONT)GetStockObject(DEFAULT_GUI_FONT), TRUE);

	HDITEM hdi;
	hdi.mask = HDI_TEXT | HDI_WIDTH | HDI_FORMAT;
	hdi.cxy = 50;
	hdi.pszText = "Cycles";
	hdi.fmt = HDF_LEFT | HDF_STRING;
	SendMessage(mhwndHeader, HDM_INSERTITEM, 0, (LPARAM)&hdi);
	hdi.pszText = "Insns";
	SendMessage(mhwndHeader, HDM_INSERTITEM, 1, (LPARAM)&hdi);
	hdi.pszText = "CPI";
	SendMessage(mhwndHeader, HDM_INSERTITEM, 2, (LPARAM)&hdi);
	hdi.pszText = "Text";
	SendMessage(mhwndHeader, HDM_INSERTITEM, 3, (LPARAM)&hdi);

	mpSourceView = new ATUIProfilerSourceTextView(mSession);
	mpSourceView->Create(mhwnd);

	OnSize();
	UpdateViewColumnWidths();
	mpSourceView->SetTargetAddress(mBaseAddress);
}

void ATUIProfilerSourcePane::OnDestroy() {
	if (mpSourceView) {
		mpSourceView->Destroy();
		mpSourceView = NULL;
	}

	if (mhwndHeader) {
		DestroyWindow(mhwndHeader);
		mhwndHeader = NULL;
	}
}

void ATUIProfilerSourcePane::OnSize() {
	RECT r;
	GetClientRect(mhwnd, &r);

	WINDOWPOS wph;
	HDLAYOUT hdl = {&r, &wph};
	SendMessage(mhwndHeader, HDM_LAYOUT, 0, (LPARAM)&hdl);

	SetWindowPos(mhwndHeader, NULL, wph.x, wph.y, wph.cx, wph.cy, SWP_NOZORDER | SWP_NOACTIVATE);

	const int count = SendMessage(mhwndHeader, HDM_GETITEMCOUNT, 0, 0);
	if (count > 1) {
		int width = r.right;

		for(int i=1; i<count; ++i) {
			HDITEM hdi = {0};
			hdi.mask = HDI_WIDTH;
			if (SendMessage(mhwndHeader, HDM_GETITEM, i-1, (LPARAM)&hdi)) {
				width -= hdi.cxy;
			}
		}

		if (width < 0)
			width = 0;

		HDITEM hdi = {0};
		hdi.mask = HDI_WIDTH;
		hdi.cxy = width;
		SendMessage(mhwndHeader, HDM_SETITEM, count-1, (LPARAM)&hdi);
	}

	if (mpSourceView)
		SetWindowPos(mpSourceView->GetHandleW32(), NULL, 0, (wph.y + wph.cy), r.right, std::max<int>(0, r.bottom - (wph.y + wph.cy)), SWP_NOZORDER | SWP_NOACTIVATE);
}

void ATUIProfilerSourcePane::UpdateViewColumnWidths() {
	int widths[3] = {0};

	HDITEM hdi = {0};
	hdi.mask = HDI_WIDTH;
	for(int i=0; i<3; ++i) {
		if (SendMessage(mhwndHeader, HDM_GETITEM, i, (LPARAM)&hdi))
			widths[i] = hdi.cxy;
	}

	mpSourceView->SetColumnWidths(widths);
}

/////////////////////////////////////////////////////////////////////////////

class ATUIProfilerPane : public ATUIPane {
public:
	ATUIProfilerPane();
	~ATUIProfilerPane();

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	bool OnCreate();
	void OnDestroy();
	void OnSize();
	bool OnCommand(uint32 id, uint32 extcode);
	bool OnNotify(uint32 id, uint32 code, const void *hdr, LRESULT& result);
	void OnColumnClicked(VDUIProxyListView *lv, int column);
	void OnItemDoubleClicked(VDUIProxyListView *lv, int item);
	void UpdateRunButtonEnables();

	void LoadProfile();
	void RemakeView();

	void VLGetText(int item, int subItem, VDStringW& s) const;

	HWND mhwndToolbar;
	HWND mhwndList;
	HMENU mhmenuMode;
	HIMAGELIST mToolbarImageList;
	ATProfileMode mProfileMode;

	ATProfileSession mSession;

	VDUIProxyListView	mListView;
	VDUIProxyMessageDispatcherW32	mDispatcher;

	friend class VLItem;
	class VLItem : public IVDUIListViewVirtualItem {
	public:
		int AddRef() { return 2; }
		int Release() { return 1; }

		void Init(int index, ATUIProfilerPane *parent) {
			mIndex = index;
			mpParent = parent;
		}

		void GetText(int subItem, VDStringW& s) const;

		int mIndex;
		ATUIProfilerPane *mpParent;
	};

	friend class VLComparer;
	class VLComparer : public IVDUIListViewVirtualComparer {
	public:
		VLComparer() {
			mSort[0] = 3;
			mSort[1] = 1;
			mSort[2] = 0;
			mSort[3] = 2;
			mSort[4] = 4;
			mDescending[0] = 0;
			mDescending[1] = 0;
			mDescending[2] = -1;
			mDescending[3] = -1;
			mDescending[4] = -1;
		}

		int Compare(IVDUIListViewVirtualItem *x, IVDUIListViewVirtualItem *y) {
			VLItem& a = *static_cast<VLItem *>(x);
			VLItem& b = *static_cast<VLItem *>(y);

			const ATProfileRecord& r = a.mpParent->mSession.mRecords[a.mIndex];
			const ATProfileRecord& s = a.mpParent->mSession.mRecords[b.mIndex];

			for(int i=0; i<5; ++i) {
				int j = mSort[i];
				int inv = mDescending[j];
				int diff;

				switch(j) {
					case 0:
						diff = (((int)(r.mAddress & 0x10000) - (int)(s.mAddress & 0x10000)) ^ inv) - inv;
						break;

					case 1:
						diff = (((int)(r.mAddress & 0xFFFF) - (int)(s.mAddress & 0xFFFF)) ^ inv) - inv;
						break;

					case 2:
						diff = (((int)r.mCalls - (int)s.mCalls) ^ inv) - inv;
						break;

					case 3:
						diff = (((int)r.mCycles - (int)s.mCycles) ^ inv) - inv;
						break;

					case 4:
						diff = (((int)r.mInsns - (int)s.mInsns) ^ inv) - inv;
						break;
				}

				if (diff)
					return diff;
			}

			return 0;
		}

		void Float(int idx) {
			if (mSort[0] == idx) {
				mDescending[idx] ^= -1;
				return;
			} else if (mSort[1] == idx) {
				// nothing
			} else if (mSort[2] == idx) {
				mSort[2] = mSort[1];
			} else if (mSort[3] == idx) {
				mSort[3] = mSort[2];
				mSort[2] = mSort[1];
			} else {
				mSort[4] = mSort[3];
				mSort[3] = mSort[2];
				mSort[2] = mSort[1];
			}

			mSort[1] = mSort[0];
			mSort[0] = idx;
		}

		uint8 mSort[5];
		sint8 mDescending[5];
	};

	VLComparer mComparer;
	vdvector<VLItem> mVLItems;

	VDDelegate mDelegateColumnClicked;
	VDDelegate mDelegateItemDoubleClicked;
};

ATUIProfilerPane::ATUIProfilerPane()
	: ATUIPane(kATUIPaneId_Profiler, "Profile View")
	, mhwndToolbar(NULL)
	, mhwndList(NULL)
	, mhmenuMode(NULL)
	, mToolbarImageList(NULL)
	, mProfileMode(kATProfileMode_Insns)
{
	mListView.OnColumnClicked() += mDelegateColumnClicked.Bind(this, &ATUIProfilerPane::OnColumnClicked);
	mListView.OnItemDoubleClicked() += mDelegateItemDoubleClicked.Bind(this, &ATUIProfilerPane::OnItemDoubleClicked);
}

ATUIProfilerPane::~ATUIProfilerPane() {
}

LRESULT ATUIProfilerPane::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_COMMAND) {
		if (OnCommand(LOWORD(wParam), HIWORD(wParam)))
			return 0;

		return mDispatcher.Dispatch_WM_COMMAND(wParam, lParam);
	} else if (msg == WM_NOTIFY) {
		const NMHDR& hdr = *(const NMHDR *)lParam;
		LRESULT res = 0;

		if (OnNotify(hdr.idFrom, hdr.code, &hdr, res))
			return res;

		return mDispatcher.Dispatch_WM_NOTIFY(wParam, lParam);
	}

	return ATUIPane::WndProc(msg, wParam, lParam);
}

bool ATUIProfilerPane::OnCreate() {
	mhmenuMode = LoadMenu(NULL, MAKEINTRESOURCE(IDR_PROFILE_MODE_MENU));

	mToolbarImageList = ImageList_LoadBitmap(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDB_TOOLBAR_PROFILER), 16, 0, RGB(255, 0, 255));

	mhwndToolbar = CreateWindow(TOOLBARCLASSNAME, _T(""), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, mhwnd, (HMENU)100, VDGetLocalModuleHandleW32(), NULL);
	if (!mhwndToolbar)
		return false;

	mhwndList = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_NOPARENTNOTIFY, WC_LISTVIEW, _T(""), WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, 0, 0, 0, 0, mhwnd, (HMENU)101, VDGetLocalModuleHandleW32(), NULL);
	
	SendMessage(mhwndToolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS);
	SendMessage(mhwndToolbar, TB_SETIMAGELIST, 0, (LPARAM)mToolbarImageList);

	TBBUTTON tbb[3] = {0};
	tbb[0].iBitmap = 0;
	tbb[0].idCommand = 1000;
	tbb[0].fsState = TBSTATE_ENABLED;
	tbb[0].fsStyle = TBSTYLE_BUTTON;
	tbb[0].dwData = 0;
	tbb[0].iString = 0;
	tbb[1].iBitmap = 1;
	tbb[1].idCommand = 1001;
	tbb[1].fsState = TBSTATE_ENABLED;
	tbb[1].fsStyle = TBSTYLE_BUTTON;
	tbb[1].dwData = 0;
	tbb[1].iString = 0;
	tbb[2].iBitmap = 2;
	tbb[2].idCommand = 1002;
	tbb[2].fsState = TBSTATE_ENABLED;
	tbb[2].fsStyle = TBSTYLE_DROPDOWN;
	tbb[2].dwData = 0;
	tbb[2].iString = 0;

	SendMessage(mhwndToolbar, TB_ADDBUTTONS, 3, (LPARAM)tbb);
	SendMessage(mhwndToolbar, TB_AUTOSIZE, 0, 0);

	mListView.Attach(mhwndList);
	mDispatcher.AddControl(&mListView);

	mListView.SetFullRowSelectEnabled(true);
	mListView.InsertColumn(0, L"Thread", 0);
	mListView.AutoSizeColumns();

	UpdateRunButtonEnables();

	return ATUIPane::OnCreate();
}

void ATUIProfilerPane::OnDestroy() {
	mDispatcher.RemoveAllControls();

	if (mhmenuMode) {
		::DestroyMenu(mhmenuMode);
		mhmenuMode = NULL;
	}

	if (mhwndList) {
		::DestroyWindow(mhwndList);
		mhwndList = NULL;
	}

	if (mhwndToolbar) {
		::DestroyWindow(mhwndToolbar);
		mhwndToolbar = NULL;
	}

	if (mToolbarImageList) {
		ImageList_Destroy(mToolbarImageList);
		mToolbarImageList = NULL;
	}

	ATUIPane::OnDestroy();
}

void ATUIProfilerPane::OnSize() {
	RECT r;
	GetClientRect(mhwnd, &r);

	int y = 0;

	if (mhwndToolbar) {
		RECT r2;
		GetWindowRect(mhwndToolbar, &r2);

		int th = r2.bottom - r2.top;
		SetWindowPos(mhwndToolbar, NULL, 0, 0, r.right, th, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

		y = th;
	}

	if (mhwndList) {
		int lh = r.bottom - y;

		if (lh < 0)
			lh = 0;

		SetWindowPos(mhwndList, NULL, 0, y, r.right, lh, SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

bool ATUIProfilerPane::OnCommand(uint32 id, uint32 extcode) {
	switch(id) {
		case 1000:
			g_sim.Suspend();
			if (ATCPUProfiler *prof = g_sim.GetProfiler()) {
				LoadProfile();
				prof->End();
				g_sim.SetProfilingEnabled(false);
			}
			UpdateRunButtonEnables();
			return true;

		case 1001:
			g_sim.SetProfilingEnabled(true);
			g_sim.GetProfiler()->Start(mProfileMode);
			UpdateRunButtonEnables();
			g_sim.Resume();
			return true;

		case 1002:
			{
				RECT r;
				SendMessage(mhwndToolbar, TB_GETRECT, 1002, (LPARAM)&r);
				MapWindowPoints(mhwndToolbar, NULL, (LPPOINT)&r, 2);

				TPMPARAMS tpm;
				tpm.cbSize = sizeof(TPMPARAMS);
				tpm.rcExclude = r;
				TrackPopupMenuEx(GetSubMenu(mhmenuMode, 0), TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_VERTICAL, r.left, r.bottom, mhwnd, &tpm);
			}
			return true;

		case ID_PROFMODE_SAMPLEINSNS:
			SendMessage(mhwndToolbar, TB_CHANGEBITMAP, 1002, 2);
			mProfileMode = kATProfileMode_Insns;
			return true;

		case ID_PROFMODE_SAMPLEFNS:
			SendMessage(mhwndToolbar, TB_CHANGEBITMAP, 1002, 3);
			mProfileMode = kATProfileMode_Functions;
			return true;

		case ID_PROFMODE_SAMPLEBASIC:
			SendMessage(mhwndToolbar, TB_CHANGEBITMAP, 1002, 4);
			mProfileMode = kATProfileMode_BasicLines;
			return true;
	}

	return false;
}

bool ATUIProfilerPane::OnNotify(uint32 id, uint32 code, const void *hdr, LRESULT& result) {
	switch(id) {
		case 100:
			if (code == TBN_DROPDOWN) {
				const NMTOOLBAR& tbhdr = *(const NMTOOLBAR *)hdr;

				if (tbhdr.iItem == 1002) {
					OnCommand(1002, 0);
					result = TBDDRET_DEFAULT;
					return true;
				}
			}
			break;
	}

	return false;
}

void ATUIProfilerPane::OnColumnClicked(VDUIProxyListView *lv, int column) {
	if (column >= 5)
		column -= 2;

	mComparer.Float(column);
	mListView.Sort(mComparer);
}

void ATUIProfilerPane::OnItemDoubleClicked(VDUIProxyListView *lv, int item) {
	const VLItem *vli = static_cast<const VLItem *>(mListView.GetVirtualItem(item));

	if (vli) {
		int idx = vli->mIndex;

		vdrefptr<ATUIProfilerSourcePane> srcPane(new ATUIProfilerSourcePane(mSession, mSession.mRecords[idx].mAddress));
		srcPane->Create(mhwnd);
	}
}

void ATUIProfilerPane::UpdateRunButtonEnables() {
	ATCPUProfiler *profiler = g_sim.GetProfiler();
	bool active = profiler && profiler->IsRunning();

	SendMessage(mhwndToolbar, TB_ENABLEBUTTON, 1000, active);
	SendMessage(mhwndToolbar, TB_ENABLEBUTTON, 1001, !active);
}

void ATUIProfilerPane::LoadProfile() {
	g_sim.GetProfiler()->GetSession(mSession);

	RemakeView();
}

void ATUIProfilerPane::RemakeView() {
	mListView.Clear();
	mVLItems.clear();
	mListView.ClearExtraColumns();

	switch(mProfileMode) {
		case kATProfileMode_Insns:
		case kATProfileMode_Functions:
			mListView.InsertColumn(1, L"Address", 0);
			break;
		case kATProfileMode_BasicLines:
			mListView.InsertColumn(1, L"Line", 0);
			break;
	}

	mListView.InsertColumn(2, L"Calls", 0);
	mListView.InsertColumn(3, L"Clocks", 0);
	mListView.InsertColumn(4, L"Insns", 0);
	mListView.InsertColumn(5, L"Clocks%", 0);
	mListView.InsertColumn(6, L"Insns%", 0);

	uint32 n = mSession.mRecords.size();
	mVLItems.resize(n);

	for(uint32 i=0; i<n; ++i) {
		mVLItems[i].Init(i, this);
		mListView.InsertVirtualItem(i, &mVLItems[i]);
	}

	mListView.AutoSizeColumns();
	mListView.Sort(mComparer);
}

void ATUIProfilerPane::VLGetText(int item, int subItem, VDStringW& s) const {
	const ATProfileRecord& record = mSession.mRecords[item];

	switch(subItem) {
		case 0:
			switch(record.mAddress & 0x30000) {
				case 0x30000:
					s = L"NMI";
					break;

				case 0x20000:
					s = L"IRQ";
					break;

				case 0x10000:
					s = L"Interrupt";
					break;

				case 0:
					s = L"Main";
					break;
			}
			break;

		case 1:
			{
				uint32 addr = record.mAddress & 0xFFFF;

				if (mProfileMode == kATProfileMode_BasicLines) {
					s.sprintf(L"%u", addr);
				} else {
					ATSymbol sym;
					if (ATGetDebuggerSymbolLookup()->LookupSymbol(addr, kATSymbol_Execute, sym) && sym.mOffset == addr)
						s.sprintf(L"%04X (%hs)", addr, sym.mpName);
					else
						s.sprintf(L"%04X", addr);
				}
			}
			break;

		case 2:
			s.sprintf(L"%u", record.mCalls);
			break;

		case 3:
			s.sprintf(L"%u", record.mCycles);
			break;

		case 4:
			s.sprintf(L"%u", record.mInsns);
			break;

		case 5:
			s.sprintf(L"%.1f%%", (float)record.mCycles / (float)mSession.mTotalCycles * 100.0f);
			break;

		case 6:
			s.sprintf(L"%.1f%%", (float)record.mInsns / (float)mSession.mTotalInsns * 100.0f);
			break;
	}
}

///////////////////////////////////////////////////////////////////////////

void ATUIProfilerPane::VLItem::GetText(int subItem, VDStringW& s) const {
	mpParent->VLGetText(mIndex, subItem, s);
}

///////////////////////////////////////////////////////////////////////////

void ATInitProfilerUI() {
	ATRegisterUIPaneType(kATUIPaneId_Profiler, VDRefCountObjectFactory<ATUIProfilerPane, ATUIPane>);
}
