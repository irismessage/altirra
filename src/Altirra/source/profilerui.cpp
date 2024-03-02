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
#include <at/atui/uiproxies.h>
#include "console.h"
#include "resource.h"
#include "simulator.h"
#include "profiler.h"
#include "debugger.h"
#include "disasm.h"

extern ATSimulator g_sim;

/////////////////////////////////////////////////////////////////////////////

class ATUIProfilerSourceTextView : public ATUINativeWindow {
public:
	ATUIProfilerSourceTextView(const ATProfileSession& profsess);
	~ATUIProfilerSourceTextView();

	bool Create(HWND hwndParent);
	void Destroy();

	void SetTargetAddress(uint32 addr);
	void SetColumnWidths(int widths[5]);

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	void OnCreate();
	void OnSize();
	void OnPaint();
	void OnVScroll(int code);
	void ScrollLines(int delta);
	void RemakeView();

	enum {
		kScrollMarginLines = 5
	};

	typedef vdfastvector<ATProfileRecord> Records;
	Records		mRecords;

	uint32		mTargetAddress;
	uint32		mLineHeight;
	uint32		mLinesVisible;
	HFONT		mFont;
	VDStringA	mBuffer;
	int			mWheelAccum;

	int			mColumnWidths[5];

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
	, mWheelAccum(0)
{
	for(Records::iterator it(mRecords.begin()), itEnd(mRecords.end()); it != itEnd; ++it) {
		ATProfileRecord& r = *it;

		r.mAddress &= 0xffffff;
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
	if (!CreateWindow(MAKEINTATOM(sWndClass), _T("Altirra Profiler - Source View"), WS_VISIBLE|WS_CHILD, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndParent, NULL, VDGetLocalModuleHandleW32(), static_cast<ATUINativeWindow *>(this)))
		return false;

	return true;
}

void ATUIProfilerSourceTextView::Destroy() {
	if (mhwnd)
		DestroyWindow(mhwnd);
}

void ATUIProfilerSourceTextView::SetTargetAddress(uint32 addr) {
	addr &= 0xffffff;

	if (mTargetAddress == addr)
		return;

	mTargetAddress = addr;

	SCROLLINFO si = {sizeof(SCROLLINFO)};
	si.fMask = SIF_POS;
	si.nPos = addr;
	SetScrollInfo(mhwnd, SB_VERT, &si, TRUE);

	RemakeView();
}

void ATUIProfilerSourceTextView::SetColumnWidths(int widths[5]) {
	mColumnWidths[0] = widths[0];
	mColumnWidths[1] = widths[0] + widths[1];
	mColumnWidths[2] = mColumnWidths[1] + widths[2];
	mColumnWidths[3] = mColumnWidths[2] + widths[3];
	mColumnWidths[4] = mColumnWidths[3] + widths[4];

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

		case WM_MOUSEWHEEL:
			{
				UINT linesPerDelta = 3;

				::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &linesPerDelta, FALSE);

				mWheelAccum += (int)GET_WHEEL_DELTA_WPARAM(wParam) * linesPerDelta;

				int linesToScroll = mWheelAccum / WHEEL_DELTA;

				if (linesToScroll) {
					mWheelAccum -= linesToScroll * WHEEL_DELTA;

					ScrollLines(-linesToScroll);
				}
			}
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
	SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
	SetBkMode(hdc, TRANSPARENT);

	Lines::const_iterator itLines(mLines.begin()), itLinesEnd(mLines.end());

	std::advance(itLines, std::min<int>(mLines.size(), kScrollMarginLines));

	ATCPUHistoryEntry hent;
	ATDisassembleCaptureRegisterContext(hent);

	int y1 = 0;
	while(y1 < ps.rcPaint.bottom && itLines != itLinesEnd) {
		const int y2 = y1 + mLineHeight;
		if (y2 > ps.rcPaint.top) {
			uint32 addr = *itLines;

			// render counts
			Records::const_iterator it(std::lower_bound(mRecords.begin(), mRecords.end(), addr, RecordSort()));

			hent.mbEmulation = true;
			hent.mP = 0xFF;

			if (it != mRecords.end() && it->mAddress == addr) {
				const ATProfileRecord& r = *it;

				hent.mP = (hent.mP & 0xCF) + (r.mModeBits << 4);
				hent.mbEmulation = r.mEmulationMode != 0;

				SetTextAlign(hdc, TA_RIGHT | TA_TOP);

				mBuffer.sprintf(" %u", r.mCycles);
				RECT rc1 = {0, y1, mColumnWidths[0], y2};
				ExtTextOutA(hdc, rc1.right, y1, ETO_CLIPPED, &rc1, mBuffer.data(), mBuffer.size(), NULL);

				mBuffer.sprintf(" %u", r.mInsns);
				RECT rc2 = {mColumnWidths[0], y1, mColumnWidths[1], y2};
				ExtTextOutA(hdc, rc2.right, y1, ETO_CLIPPED, &rc2, mBuffer.data(), mBuffer.size(), NULL);

				mBuffer.sprintf(" %.1f", r.mInsns ? (float)r.mCycles / (float)r.mInsns : 0.0f);
				RECT rc3 = {mColumnWidths[1], y1, mColumnWidths[2], y2};
				ExtTextOutA(hdc, rc3.right, y1, ETO_CLIPPED, &rc3, mBuffer.data(), mBuffer.size(), NULL);

				mBuffer.sprintf(" %.1f", r.mInsns ? (float)r.mUnhaltedCycles / (float)r.mInsns : 0.0f);
				RECT rc4 = {mColumnWidths[2], y1, mColumnWidths[3], y2};
				ExtTextOutA(hdc, rc4.right, y1, ETO_CLIPPED, &rc4, mBuffer.data(), mBuffer.size(), NULL);

				mBuffer.sprintf(" %.1f%%", r.mCycles ? 100.0f * (1.0f - (float)r.mUnhaltedCycles / (float)r.mCycles) : 0.0f);
				RECT rc5 = {mColumnWidths[3], y1, mColumnWidths[4], y2};
				ExtTextOutA(hdc, rc5.right, y1, ETO_CLIPPED, &rc5, mBuffer.data(), mBuffer.size(), NULL);
			}

			// render line
			mBuffer = " ";

			ATDisassembleCaptureInsnContext((uint16)addr, (uint8)(addr >> 16), hent);

			ATDisassembleInsn(mBuffer, hent, false, false, true, true, true);

			SetTextAlign(hdc, TA_LEFT | TA_TOP);
			ExtTextOutA(hdc, mColumnWidths[4], y1, 0, NULL, mBuffer.data(), mBuffer.size(), NULL);
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
			newPos = mLines[(mLinesVisible >> 1) - 1 + kScrollMarginLines];
			break;
		case SB_LINEDOWN:
			newPos = mLines[(mLinesVisible >> 1) + 1 + kScrollMarginLines];
			break;
		case SB_PAGEUP:
			newPos = mLines[std::min<int>(mLines.size(), kScrollMarginLines + 1) - 1];
			break;
		case SB_PAGEDOWN:
			newPos = mLines[std::max<int>(mLines.size(), kScrollMarginLines + 1) - (kScrollMarginLines + 1)];
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

void ATUIProfilerSourceTextView::ScrollLines(int delta) {
	SCROLLINFO si={sizeof(SCROLLINFO)};
	si.fMask = SIF_POS | SIF_TRACKPOS | SIF_RANGE;
	if (!GetScrollInfo(mhwnd, SB_VERT, &si))
		return;

	int idx = std::min<int>(kScrollMarginLines + (mLinesVisible >> 1) + delta, (int)mLines.size() - 1);

	if (idx < 0)
		idx = 0;

	int newPos = mLines[idx];

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
	uint32 linesAbove = (mLinesVisible >> 1) + kScrollMarginLines;
	uint32 bank = mTargetAddress & 0xff0000;
	uint32 nextAddr = (mTargetAddress - linesAbove*3) & 0xffff;
	uint32 stepAddr = nextAddr;

	mLines.clear();

	// compute lines prior to target address
	while(stepAddr + bank != mTargetAddress) {
		if (nextAddr != stepAddr) {
			Records::const_iterator it(std::lower_bound(mRecords.begin(), mRecords.end(), bank+stepAddr, RecordSort()));

			if (it == mRecords.end() || it->mAddress != bank + stepAddr) {
				++stepAddr;
				stepAddr &= 0xffff;
				continue;
			}
		}

		const Records::const_iterator it2 = std::lower_bound(mRecords.begin(), mRecords.end(), bank+nextAddr, RecordSort());
		const uint8 opcode = g_sim.DebugGlobalReadByte(bank + nextAddr);

		if (it2 != mRecords.end() && it2->mAddress == bank + nextAddr)
			nextAddr += ATGetOpcodeLength(opcode, it2->mModeBits << 4, it2->mEmulationMode != 0);
		else
			nextAddr += ATGetOpcodeLength(opcode);

		nextAddr &= 0xffff;

		mLines.push_back(bank + stepAddr);

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
	while(n < mLinesVisible + kScrollMarginLines*2) {
		if (nextAddr != stepAddr) {
			Records::const_iterator it(std::lower_bound(mRecords.begin(), mRecords.end(), bank+stepAddr, RecordSort()));

			if (it == mRecords.end() || it->mAddress != bank + stepAddr) {
				++stepAddr;
				stepAddr &= 0xffff;
				continue;
			}
		}

		const Records::const_iterator it2 = std::lower_bound(mRecords.begin(), mRecords.end(), bank+nextAddr, RecordSort());
		const uint8 opcode = g_sim.DebugGlobalReadByte(bank + nextAddr);

		if (it2 != mRecords.end() && it2->mAddress == bank + nextAddr)
			nextAddr += ATGetOpcodeLength(opcode, it2->mModeBits << 4, it2->mEmulationMode != 0);
		else
			nextAddr += ATGetOpcodeLength(opcode);

		nextAddr &= 0xffff;

		mLines.push_back(bank + stepAddr);
		++n;
		++stepAddr;
		stepAddr &= 0xffff;
	}

	if (mhwnd)
		InvalidateRect(mhwnd, NULL, TRUE);
}

/////////////////////////////////////////////////////////////////////////////

class ATUIProfilerSourcePane : public ATUINativeWindow {
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
	if (!CreateWindow(MAKEINTATOM(sWndClass), _T("Altirra Profiler - Source View"), WS_VISIBLE|WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndParent, NULL, VDGetLocalModuleHandleW32(), static_cast<ATUINativeWindow *>(this)))
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

	return ATUINativeWindow::WndProc(msg, wParam, lParam);
}

void ATUIProfilerSourcePane::OnCreate() {
	mhwndHeader = CreateWindow(WC_HEADER, _T(""), WS_CHILD | WS_VISIBLE | HDS_HORZ | HDS_BUTTONS | HDS_FULLDRAG, 0, 0, 0, 0, mhwnd, (HMENU)100, VDGetLocalModuleHandleW32(), NULL);

	SendMessage(mhwndHeader, WM_SETFONT, (WPARAM)(HFONT)GetStockObject(DEFAULT_GUI_FONT), TRUE);

	HDITEM hdi;
	hdi.mask = HDI_TEXT | HDI_WIDTH | HDI_FORMAT;
	hdi.cxy = 50;
	hdi.pszText = (LPTSTR)_T("Cycles");
	hdi.fmt = HDF_LEFT | HDF_STRING;
	SendMessage(mhwndHeader, HDM_INSERTITEM, 0, (LPARAM)&hdi);
	hdi.pszText = (LPTSTR)_T("Insns");
	SendMessage(mhwndHeader, HDM_INSERTITEM, 1, (LPARAM)&hdi);
	hdi.pszText = (LPTSTR)_T("CPI");
	SendMessage(mhwndHeader, HDM_INSERTITEM, 2, (LPARAM)&hdi);
	hdi.pszText = (LPTSTR)_T("CCPI");
	SendMessage(mhwndHeader, HDM_INSERTITEM, 3, (LPARAM)&hdi);
	hdi.pszText = (LPTSTR)_T("DMA%");
	SendMessage(mhwndHeader, HDM_INSERTITEM, 4, (LPARAM)&hdi);
	hdi.pszText = (LPTSTR)_T("Text");
	SendMessage(mhwndHeader, HDM_INSERTITEM, 5, (LPARAM)&hdi);

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
	int widths[5] = {0};

	HDITEM hdi = {0};
	hdi.mask = HDI_WIDTH;
	for(int i=0; i<5; ++i) {
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
	void OnItemSelectionChanged(VDUIProxyListView *lv, int item);
	void OnTreeItemDoubleClicked(VDUIProxyTreeViewControl *tv, bool *handled);
	void UpdateRunButtonEnables();

	void LoadProfile();
	void RemakeView();

	void VLGetText(int item, int subItem, VDStringW& s) const;
	void VTGetText(int item, VDStringW& s) const;

	HWND mhwndToolbar;
	HWND mhwndList;
	HWND mhwndTree;
	HWND mhwndStatus;
	HMENU mhmenuMode;
	HIMAGELIST mToolbarImageList;
	ATProfileMode mProfileMode;

	ATProfileSession mSession;

	VDUIProxyListView	mListView;
	VDUIProxyTreeViewControl	mTreeView;
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
			mSort[5] = 5;
			mSort[6] = 6;
			mDescending[0] = 0;
			mDescending[1] = 0;
			mDescending[2] = -1;
			mDescending[3] = -1;
			mDescending[4] = -1;
			mDescending[5] = -1;
			mDescending[6] = -1;
		}

		int Compare(IVDUIListViewVirtualItem *x, IVDUIListViewVirtualItem *y) {
			VLItem& a = *static_cast<VLItem *>(x);
			VLItem& b = *static_cast<VLItem *>(y);

			const ATProfileRecord& r = a.mpParent->mSession.mRecords[a.mIndex];
			const ATProfileRecord& s = a.mpParent->mSession.mRecords[b.mIndex];

			for(int i=0; i<6; ++i) {
				int j = mSort[i];
				int inv = mDescending[j];
				int diff;

				switch(j) {
					case 0:
						diff = (((int)(r.mAddress & 0x7000000) - (int)(s.mAddress & 0x7000000)) ^ inv) - inv;
						break;

					case 1:
						diff = (((int)(r.mAddress & 0xFFFFFF) - (int)(s.mAddress & 0xFFFFFF)) ^ inv) - inv;
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

					case 5:
						diff = (((int)r.mUnhaltedCycles - (int)s.mUnhaltedCycles) ^ inv) - inv;
						break;

					case 6:
						// diff = r.u / r.c - s.u / s.c
						//      = [r.u*s.c - s.u*r.c] / (r.c * s.c);
						{
							float discriminant = (float)r.mUnhaltedCycles * (float)s.mCycles - (float)s.mUnhaltedCycles * (float)r.mCycles;

							diff = (discriminant < 0.0f) ? -1 : (discriminant > 0.0f) ? +1 : 0;

							diff = (diff ^ inv) - inv;
						}
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
			}

			for(int i=1; i<vdcountof(mSort); ++i) {
				if (mSort[i] == idx) {
					memmove(mSort + 1, mSort, i);
					mSort[0] = idx;
					break;
				}
			}
		}

		uint8 mSort[7];
		sint8 mDescending[7];
	};

	friend class VTItem;
	class VTItem : public IVDUITreeViewVirtualItem {
	public:
		int AddRef() { return 2; }
		int Release() { return 1; }

		void *AsInterface(uint32) { return NULL; }

		void Init(int index, ATUIProfilerPane *parent) {
			mIndex = index;
			mpParent = parent;
		}

		void GetText(VDStringW& s) const;

		int mIndex;
		ATUIProfilerPane *mpParent;
	};

	VLComparer mComparer;
	vdvector<VLItem> mVLItems;
	vdvector<VTItem> mVTItems;

	VDDelegate mDelegateColumnClicked;
	VDDelegate mDelegateItemDoubleClicked;
	VDDelegate mDelegateItemSelectionChanged;
	VDDelegate mDelegateTreeItemDoubleClicked;
};

ATUIProfilerPane::ATUIProfilerPane()
	: ATUIPane(kATUIPaneId_Profiler, L"Profile View")
	, mhwndToolbar(NULL)
	, mhwndList(NULL)
	, mhwndTree(NULL)
	, mhwndStatus(NULL)
	, mhmenuMode(NULL)
	, mToolbarImageList(NULL)
	, mProfileMode(kATProfileMode_Insns)
{
	mListView.OnColumnClicked() += mDelegateColumnClicked.Bind(this, &ATUIProfilerPane::OnColumnClicked);
	mListView.OnItemDoubleClicked() += mDelegateItemDoubleClicked.Bind(this, &ATUIProfilerPane::OnItemDoubleClicked);
	mListView.OnItemSelectionChanged() += mDelegateItemSelectionChanged.Bind(this, &ATUIProfilerPane::OnItemSelectionChanged);
	mTreeView.OnItemDoubleClicked() += mDelegateTreeItemDoubleClicked.Bind(this, &ATUIProfilerPane::OnTreeItemDoubleClicked);
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
	} else if (msg == WM_USER + 100) {
		const VTItem *vti = static_cast<VTItem *>(mTreeView.GetSelectedVirtualItem());

		if (vti) {
			int idx = vti->mIndex;

			vdrefptr<ATUIProfilerSourcePane> srcPane(new ATUIProfilerSourcePane(mSession, mSession.mCallGraphRecords[idx].mAddress));
			srcPane->Create(mhwnd);
		}

		return 0;
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

	mhwndTree = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_NOPARENTNOTIFY, WC_TREEVIEW, _T(""), WS_CHILD | WS_VISIBLE | TVS_FULLROWSELECT | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 0, 0, mhwnd, (HMENU)103, VDGetLocalModuleHandleW32(), NULL);

	mhwndStatus = CreateWindowEx(0, STATUSCLASSNAME, _T(""), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, mhwnd, (HMENU)102, VDGetLocalModuleHandleW32(), NULL);
	SendMessage(mhwndStatus, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
	SetWindowPos(mhwndStatus, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	
	SendMessage(mhwndToolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS);
	SendMessage(mhwndToolbar, TB_SETIMAGELIST, 0, (LPARAM)mToolbarImageList);
	SendMessage(mhwndToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

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

	mTreeView.Attach(mhwndTree);
	mDispatcher.AddControl(&mTreeView);

	UpdateRunButtonEnables();

	return ATUIPane::OnCreate();
}

void ATUIProfilerPane::OnDestroy() {
	mDispatcher.RemoveAllControls(true);

	if (mhmenuMode) {
		::DestroyMenu(mhmenuMode);
		mhmenuMode = NULL;
	}

	if (mhwndTree) {
		::DestroyWindow(mhwndTree);
		mhwndTree = NULL;
	}

	if (mhwndList) {
		::DestroyWindow(mhwndList);
		mhwndList = NULL;
	}

	if (mhwndToolbar) {
		::DestroyWindow(mhwndToolbar);
		mhwndToolbar = NULL;
	}

	if (mhwndStatus) {
		::DestroyWindow(mhwndStatus);
		mhwndStatus = NULL;
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

	int statusY = 0;
	if (mhwndStatus) {
		RECT rs;
		if (GetWindowRect(mhwndStatus, &rs)) {
			int statusH = rs.bottom - rs.top;
			statusY = std::max<int>(0, r.bottom - statusH);

			SetWindowPos(mhwndStatus, NULL, 0, statusY, r.right, statusH, SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}

	int lh = statusY - y;

	if (lh < 0)
		lh = 0;

	if (mhwndList)
		SetWindowPos(mhwndList, NULL, 0, y, r.right, lh, SWP_NOZORDER | SWP_NOACTIVATE);

	if (mhwndTree)
		SetWindowPos(mhwndTree, NULL, 0, y, r.right, lh, SWP_NOZORDER | SWP_NOACTIVATE);
}

bool ATUIProfilerPane::OnCommand(uint32 id, uint32 extcode) {
	switch(id) {
		case 1000:
			g_sim.Suspend();
			if (ATCPUProfiler *prof = g_sim.GetProfiler()) {
				prof->End();
				LoadProfile();
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

		case ID_PROFMODE_CALLGRAPH:
			SendMessage(mhwndToolbar, TB_CHANGEBITMAP, 1002, 5);
			mProfileMode = kATProfileMode_CallGraph;
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
	static const uint8 kSortColumnTable[]={
		0, 1, 2, 3, 4, 3, 4, 5, 5, 6
	};

	if ((unsigned)column < vdcountof(kSortColumnTable)) {
		mComparer.Float(kSortColumnTable[column]);
		mListView.Sort(mComparer);
	}
}

void ATUIProfilerPane::OnItemDoubleClicked(VDUIProxyListView *lv, int item) {
	const VLItem *vli = static_cast<const VLItem *>(mListView.GetVirtualItem(item));

	if (vli) {
		int idx = vli->mIndex;

		vdrefptr<ATUIProfilerSourcePane> srcPane(new ATUIProfilerSourcePane(mSession, mSession.mRecords[idx].mAddress));
		srcPane->Create(mhwnd);
	}
}

void ATUIProfilerPane::OnItemSelectionChanged(VDUIProxyListView *lv, int item) {
	vdfastvector<int> selectedIndices;

	lv->GetSelectedIndices(selectedIndices);

	if (selectedIndices.empty()) {
		if (mhwndStatus)
			SetWindowText(mhwndStatus, _T(""));

		return;
	}

	uint32 insns = 0;
	uint32 cycles = 0;
	uint32 count = selectedIndices.size();
	while(!selectedIndices.empty()) {
		int lvIndex = selectedIndices.back();
		selectedIndices.pop_back();

		VLItem *vli = static_cast<VLItem *>(lv->GetVirtualItem(lvIndex));

		if (!vli)
			continue;

		ATProfileRecord& rec = mSession.mRecords[vli->mIndex];
		insns += rec.mInsns;
		cycles += rec.mCycles;
	}

	VDSetWindowTextFW32(mhwndStatus
		, L"Selected %u item%ls: %u cycles (%.2f%%), %u insns (%.2f%%)"
		, count
		, count == 1 ? L"" : L"s"
		, cycles
		, mSession.mTotalCycles ? (float)cycles * 100.0f / (float)mSession.mTotalCycles : 0
		, insns
		, mSession.mTotalInsns ? (float)insns * 100.0f / (float)mSession.mTotalInsns : 0
		);
}

void ATUIProfilerPane::OnTreeItemDoubleClicked(VDUIProxyTreeViewControl *tv, bool *handled) {
	*handled = true;

	// We can't open the window here as the tree view steals focus afterward.
	::PostMessage(mhwnd, WM_USER + 100, 0, 0);
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

namespace {
	struct CallGraphRecordSorter {
		CallGraphRecordSorter(ATProfileCallGraphRecord *records)
			: mpRecords(records) {}

		bool operator()(uint32 idx1, uint32 idx2) const {
			return mpRecords[idx1].mInclusiveCycles > mpRecords[idx2].mInclusiveCycles;
		}

		ATProfileCallGraphRecord *const mpRecords;
	};
}

void ATUIProfilerPane::RemakeView() {
	mListView.SetRedraw(false);
	mTreeView.SetRedraw(false);
	mListView.Clear();
	mTreeView.Clear();
	mVLItems.clear();
	mVTItems.clear();
	mListView.ClearExtraColumns();

	if (mProfileMode == kATProfileMode_CallGraph) {
		::ShowWindow(mhwndList, SW_HIDE);
		::ShowWindow(mhwndTree, SW_SHOWNOACTIVATE);

		const uint32 n = mSession.mCallGraphRecords.size();

		VDStringW s;
		vdfastvector<VDUIProxyTreeViewControl::NodeRef> nodes(n, VDUIProxyTreeViewControl::kNodeRoot);
		vdfastvector<uint32> nextSibling(n, 0);
		vdfastvector<uint32> firstChild(n, 0);
		for(uint32 i = 4; i < n; ++i) {
			const ATProfileCallGraphRecord& cgr = mSession.mCallGraphRecords[i];

			nextSibling[i] = firstChild[cgr.mParent];
			firstChild[cgr.mParent] = i;
		}

		vdfastvector<uint32> stack(4);
		stack[0] = 0;
		stack[1] = 1;
		stack[2] = 2;
		stack[3] = 3;
		std::sort(stack.begin(), stack.end(), CallGraphRecordSorter(mSession.mCallGraphRecords.data()));

		mVTItems.resize(n);

		while(!stack.empty()) {
			uint32 i = stack.back();
			stack.pop_back();

			const ATProfileCallGraphRecord& cgr = mSession.mCallGraphRecords[i];

			VTItem *pvi = &mVTItems[i];
			pvi->Init(i, this);
			nodes[i] = mTreeView.AddVirtualItem(i < 4 ? VDUIProxyTreeViewControl::kNodeRoot : nodes[cgr.mParent], VDUIProxyTreeViewControl::kNodeFirst, pvi);

			uint32 childBase = stack.size();

			for(uint32 j = firstChild[i]; j; j = nextSibling[j])
				stack.push_back(j);

			std::sort(stack.begin() + childBase, stack.end(), CallGraphRecordSorter(mSession.mCallGraphRecords.data()));
		}
	} else {
		::ShowWindow(mhwndList, SW_SHOWNOACTIVATE);
		::ShowWindow(mhwndTree, SW_HIDE);

		switch(mProfileMode) {
			case kATProfileMode_Insns:
			case kATProfileMode_Functions:
				mListView.InsertColumn(1, L"Address", 0);
				break;
			case kATProfileMode_BasicLines:
				mListView.InsertColumn(1, L"Line", 0);
				break;
		}

		mListView.InsertColumn(2, L"Calls", 0, true);
		mListView.InsertColumn(3, L"Clocks", 0, true);
		mListView.InsertColumn(4, L"Insns", 0, true);
		mListView.InsertColumn(5, L"Clocks%", 0, true);
		mListView.InsertColumn(6, L"Insns%", 0, true);
		mListView.InsertColumn(7, L"CPUClocks", 0, true);
		mListView.InsertColumn(8, L"CPUClocks%", 0, true);
		mListView.InsertColumn(9, L"DMA%", 0, true);
		mListView.InsertColumn(10, L"", 0, false);		// crude hack to fix full justified column

		uint32 n = mSession.mRecords.size();
		mVLItems.resize(n);

		for(uint32 i=0; i<n; ++i) {
			mVLItems[i].Init(i, this);
			mListView.InsertVirtualItem(i, &mVLItems[i]);
		}

		mListView.AutoSizeColumns();
		mListView.Sort(mComparer);
	}

	mTreeView.SetRedraw(true);
	mListView.SetRedraw(true);
}

void ATUIProfilerPane::VLGetText(int item, int subItem, VDStringW& s) const {
	const ATProfileRecord& record = mSession.mRecords[item];

	switch(subItem) {
		case 0:
			switch(record.mAddress & 0x7000000) {
				case 0x4000000:
					s = L"VBI";
					break;

				case 0x3000000:
					s = L"DLI";
					break;

				case 0x2000000:
					s = L"IRQ";
					break;

				case 0x1000000:
					s = L"Interrupt";
					break;

				case 0:
					s = L"Main";
					break;
			}
			break;

		case 1:
			{
				uint32 addr = record.mAddress & 0xFFFFFF;

				if (mProfileMode == kATProfileMode_BasicLines) {
					s.sprintf(L"%u", addr);
				} else if (addr >= 0x10000) {
					s.sprintf(L"%02X:%04X", addr >> 16, addr & 0xffff);
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
			s.sprintf(L"%.2f%%", (float)record.mCycles / (float)mSession.mTotalCycles * 100.0f);
			break;

		case 6:
			s.sprintf(L"%.2f%%", (float)record.mInsns / (float)mSession.mTotalInsns * 100.0f);
			break;

		case 7:
			s.sprintf(L"%u", record.mUnhaltedCycles);
			break;

		case 8:
			s.sprintf(L"%.2f%%", (float)record.mUnhaltedCycles / (float)mSession.mTotalUnhaltedCycles * 100.0f);
			break;

		case 9:
			if (record.mCycles)
				s.sprintf(L"%.2f%%", 100.0f * (1.0f - (float)record.mUnhaltedCycles / (float)record.mCycles));
			break;
	}
}

void ATUIProfilerPane::VTGetText(int item, VDStringW& s) const {
	const ATProfileCallGraphRecord& cgr = mSession.mCallGraphRecords[item];

	switch(item) {
		case 0:
			s = L"Main";
			break;

		case 1:
			s = L"IRQ";
			break;

		case 2:
			s = L"NMI (DLI)";
			break;

		case 3:
			s = L"NMI (VBI)";
			break;

		default:
			{
				sint32 addr = cgr.mAddress & 0xffffff;

				if (addr >= 0x10000) {
					s.sprintf(L"%02X:%04X", addr >> 16, addr & 0xffff);
				} else {
					s.sprintf(L"%04X", addr);

					ATSymbol sym;
					if (ATGetDebuggerSymbolLookup()->LookupSymbol(addr, kATSymbol_Execute, sym) && sym.mOffset == addr)
						s.append_sprintf(L" (%hs)", sym.mpName);
				}

				s.append_sprintf(L" [x%u]", cgr.mCalls);
			}
			break;
	}

	const float cyclesToPercent = mSession.mTotalCycles ? 100.0f / (float)mSession.mTotalCycles : 0;
	const float insnsToPercent = mSession.mTotalInsns ? 100.0f / (float)mSession.mTotalInsns : 0;

	s.append_sprintf(L": %u cycles (%.2f%%), %u insns (%.2f%%)"
		, cgr.mInclusiveCycles
		, (float)cgr.mInclusiveCycles * cyclesToPercent
		, cgr.mInclusiveInsns
		, (float)cgr.mInclusiveInsns * insnsToPercent
		);
}

///////////////////////////////////////////////////////////////////////////

void ATUIProfilerPane::VLItem::GetText(int subItem, VDStringW& s) const {
	mpParent->VLGetText(mIndex, subItem, s);
}

///////////////////////////////////////////////////////////////////////////

void ATUIProfilerPane::VTItem::GetText(VDStringW& s) const {
	mpParent->VTGetText(mIndex, s);
}

///////////////////////////////////////////////////////////////////////////

void ATInitProfilerUI() {
	ATRegisterUIPaneType(kATUIPaneId_Profiler, VDRefCountObjectFactory<ATUIProfilerPane, ATUIPane>);
}
