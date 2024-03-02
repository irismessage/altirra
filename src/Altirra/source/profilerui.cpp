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

#include <stdafx.h>
#include <numeric>
#include <vd2/system/error.h>
#include <vd2/system/math.h>
#include <vd2/system/w32assist.h>
#include <windows.h>
#include <commctrl.h>
#include <tchar.h>
#include <at/atnativeui/uiframe.h>
#include <at/atnativeui/uiproxies.h>
#include <at/atnativeui/dialog.h>
#include "console.h"
#include "resource.h"
#include "simulator.h"
#include "profiler.h"
#include "debugger.h"
#include "disasm.h"
#include "oshelper.h"

extern ATSimulator g_sim;

/////////////////////////////////////////////////////////////////////////////

class ATUIProfilerTimelineView final : public ATUINativeWindow {
public:
	void SetSession(ATProfileSession *session);

	void SetSelectedRange(sint32 start, sint32 end) { SetSelectedRange(start, end, true); }

	void SetVerticalRange(uint32 cycles);

	void SetOnRangeSelected(const vdfunction<void(uint32, uint32)>& fn) { mpOnRangeSelected = fn; }

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	void OnCreate();
	void OnSize();
	bool OnPreKeyDown(WPARAM wParam, LPARAM lParam);
	bool OnPreKeyUp(WPARAM wParam, LPARAM lParam);
	void OnMouseLeave();
	void OnMouseWheel(WPARAM wParam, LPARAM lParam);
	void OnMouseMove(WPARAM wParam, LPARAM lParam);
	void OnLButtonDown(WPARAM wParam, LPARAM lParam);
	void OnLButtonUp(WPARAM wParam, LPARAM lParam);
	void OnHScroll(WPARAM wParam, LPARAM lParam);
	void OnErase(HDC hdc);
	void OnPaint();

	void RecomputeScrollBar();
	void ScrollTo(sint32 pos);
	void SetZoomIndex(sint32 index, sint32 focusX);

	void SetHoverPosition(sint32 pos);
	void SetSelectedRange(sint32 start, sint32 end, bool notify);

	sint32 PointToFrame(sint32 x, sint32 y, bool clamp) const;
	RECT FrameToRect(sint32 pos) const;
	RECT FrameRangeToRect(sint32 start, sint32 end) const;

	ATProfileSession *mpSession = nullptr;
	sint32 mTotalFrames = 0;
	sint32 mWidth = 0;
	sint32 mHeight = 0;
	uint32 mDragStartPos = 0;
	sint32 mHoverPos = -1;
	sint32 mHotSelectionStart = 0;
	sint32 mHotSelectionEnd = 0;
	sint32 mSelectionStart = 0;
	sint32 mSelectionEnd = 0;
	sint32 mPixelsPerFrame = 1;
	sint32 mZoomIndex = 0;
	sint32 mScrollX = 0;
	uint32 mVerticalRange = 1;
	RECT mHoverRect = RECT { 0, 0, 0, 0 };
	RECT mSelectionRect = RECT { 0, 0, 0, 0 };
	bool mbDragging = false;
	bool mbTrackingMouse = false;
	sint32 mWheelAccum = 0;

	vdfunction<void(uint32, uint32)> mpOnRangeSelected;

	static const int kZoomFactors[];
};

const int ATUIProfilerTimelineView::kZoomFactors[]={
	1,2,3,4,6,8,10,12,14,16,20,24,28,32
};

void ATUIProfilerTimelineView::SetVerticalRange(uint32 cycles) {
	// don't tempt fate
	if (!cycles)
		cycles = 1;

	if (mVerticalRange != cycles) {
		mVerticalRange = cycles;

		if (mhwnd)
			InvalidateRect(mhwnd, NULL, TRUE);
	}
}

void ATUIProfilerTimelineView::SetSession(ATProfileSession *session) {
	if (mpSession != session) {
		mpSession = session;

		if (session)
			mTotalFrames = (sint32)session->mpFrames.size();
		else
			mTotalFrames = 0;

		// auto-select scroll factor
		if (mTotalFrames > 0) {
			auto it = std::upper_bound(std::begin(kZoomFactors), std::end(kZoomFactors), mWidth / mTotalFrames);

			SetZoomIndex(std::max(0, (int)(it - std::begin(kZoomFactors)) - 1), 0);
		}

		ScrollTo(0);

		if (mhwnd) {
			InvalidateRect(mhwnd, NULL, TRUE);
			RecomputeScrollBar();
		}
	}
}

LRESULT ATUIProfilerTimelineView::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			OnCreate();
			break;

		case WM_SIZE:
			OnSize();
			break;

		case WM_ERASEBKGND:
			OnErase((HDC)wParam);
			return 0;

		case WM_PAINT:
			OnPaint();
			return 0;

		case WM_MOUSEWHEEL:
			OnMouseWheel(wParam, lParam);
			return 0;

		case WM_MOUSELEAVE:
			OnMouseLeave();
			return 0;

		case WM_MOUSEMOVE:
			OnMouseMove(wParam, lParam);
			return 0;

		case WM_LBUTTONDOWN:
			OnLButtonDown(wParam, lParam);
			return 0;

		case WM_LBUTTONUP:
			OnLButtonUp(wParam, lParam);
			return 0;

		case WM_HSCROLL:
			OnHScroll(wParam, lParam);
			return 0;

		case ATWM_PREKEYDOWN:
			return OnPreKeyDown(wParam, lParam);

		case ATWM_PREKEYUP:
			return OnPreKeyUp(wParam, lParam);
	}

	return DefWindowProc(mhwnd, msg, wParam, lParam);
}

void ATUIProfilerTimelineView::OnCreate() {
	ShowScrollBar(mhwnd, SB_HORZ, TRUE);

	OnSize();
}

void ATUIProfilerTimelineView::OnSize() {
	RECT r;

	if (GetClientRect(mhwnd, &r)) {
		if (mHeight != r.bottom) {
			mHeight = r.bottom;

			InvalidateRect(mhwnd, NULL, TRUE);
		}

		if (mWidth != r.right) {
			mWidth = r.right;

			// check if we need to scroll
			RecomputeScrollBar();
		}
	}
}

bool ATUIProfilerTimelineView::OnPreKeyDown(WPARAM wParam, LPARAM lParam) {
	if (wParam == VK_OEM_PLUS || wParam == VK_ADD) {
		SetZoomIndex(mZoomIndex + 1, -1);
		return true;
	}

	if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) {
		SetZoomIndex(mZoomIndex - 1, -1);
		return true;
	}

	return false;
}

bool ATUIProfilerTimelineView::OnPreKeyUp(WPARAM wParam, LPARAM lParam) {
	switch(wParam) {
		case VK_OEM_PLUS:
		case VK_OEM_MINUS:
		case VK_ADD:
		case VK_SUBTRACT:
			return true;
	}

	return false;
}

void ATUIProfilerTimelineView::OnMouseLeave() {
	mbTrackingMouse = false;

	SetHoverPosition(-1);
}

void ATUIProfilerTimelineView::OnMouseMove(WPARAM wParam, LPARAM lParam) {
	if (!mbTrackingMouse) {
		mbTrackingMouse = true;

		TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, mhwnd, 0 };
		TrackMouseEvent(&tme);
	}

	const sint32 x = (short)LOWORD(lParam);
	const sint32 y = (short)HIWORD(lParam);

	if (mbDragging) {
		sint32 pos = PointToFrame(x, y, true);
		if (pos >= 0) {
			uint32 dragEndPos = (uint32)pos;
			uint32 selStart, selEnd;

			if (mDragStartPos <= dragEndPos) {
				selStart = mDragStartPos;
				selEnd = dragEndPos + 1;
			} else {
				selStart = dragEndPos;
				selEnd = mDragStartPos + 1;
			}

			SetSelectedRange(selStart, selEnd, false);
		}
	} else {
		SetHoverPosition(PointToFrame(x, y, false));
	}
}

void ATUIProfilerTimelineView::OnMouseWheel(WPARAM wParam, LPARAM lParam) {
	const sint32 x = (short)LOWORD(lParam);
	const sint32 y = (short)HIWORD(lParam);

	mWheelAccum += (short)HIWORD(wParam);

	int deltas = mWheelAccum / WHEEL_DELTA;

	if (deltas) {
		mWheelAccum -= deltas * WHEEL_DELTA;

		POINT pt = { x, y };
		ScreenToClient(mhwnd, &pt);

		SetZoomIndex(mZoomIndex + deltas, pt.x);
	}
}

void ATUIProfilerTimelineView::OnLButtonDown(WPARAM wParam, LPARAM lParam) {
	const sint32 x = (short)LOWORD(lParam);
	const sint32 y = (short)HIWORD(lParam);

	::SetFocus(mhwnd);

	sint32 pos = PointToFrame(x, y, false);

	if (pos >= 0) {
		mbDragging = true;
		mDragStartPos = (uint32)pos;

		SetHoverPosition(-1);
		::SetCapture(mhwnd);
	}
}

void ATUIProfilerTimelineView::OnLButtonUp(WPARAM wParam, LPARAM lParam) {
	const sint32 x = (short)LOWORD(lParam);
	const sint32 y = (short)HIWORD(lParam);

	if (mbDragging) {
		::ReleaseCapture();

		mbDragging = false;

		sint32 pos = PointToFrame(x, y, true);
		if (pos >= 0) {
			uint32 dragEndPos = (uint32)pos;
			uint32 selStart, selEnd;

			if (mDragStartPos <= dragEndPos) {
				selStart = mDragStartPos;
				selEnd = dragEndPos + 1;
			} else {
				selStart = dragEndPos;
				selEnd = mDragStartPos + 1;
			}

			SetSelectedRange(selStart, selEnd, true);
		}
	}
}

void ATUIProfilerTimelineView::OnHScroll(WPARAM wParam, LPARAM lParam) {
	SCROLLINFO si = {sizeof(SCROLLINFO), SIF_POS | SIF_TRACKPOS};
	GetScrollInfo(mhwnd, SB_HORZ, &si);

	sint32 xs = si.nPos;
	sint32 xmax = mTotalFrames * mPixelsPerFrame;

	switch(LOWORD(wParam)) {
		case SB_LEFT:
			xs = 0;
			break;

		case SB_RIGHT:
			xs = xmax;
			break;

		case SB_LINELEFT:
			xs -= mPixelsPerFrame;
			break;

		case SB_LINERIGHT:
			xs += mPixelsPerFrame;
			break;

		case SB_PAGELEFT:
			xs -= mWidth;
			break;

		case SB_PAGERIGHT:
			xs += mWidth;
			break;

		case SB_THUMBPOSITION:
		case SB_THUMBTRACK:
			xs = si.nTrackPos;
			break;
	}

	ScrollTo(xs);
}

void ATUIProfilerTimelineView::OnErase(HDC hdc) {
	RECT r;

	if (GetClientRect(mhwnd, &r)) {
		SetBkColor(hdc, 0x202020);
		SetBkMode(hdc, OPAQUE);
		ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &r, _T(""), 0, nullptr);
	}
}

void ATUIProfilerTimelineView::OnPaint() {
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(mhwnd, &ps);
	if (!hdc)
		return;

	RECT r;
	if (mpSession && GetClientRect(mhwnd, &r)) {
		int x1 = ps.rcPaint.left;
		int x2 = ps.rcPaint.right;
		sint32 frame1 = std::max<sint32>((x1 + mScrollX) / mPixelsPerFrame, 0);
		sint32 frame2 = std::min<sint32>((x2 + mScrollX + mPixelsPerFrame - 1) / mPixelsPerFrame, (sint32)mpSession->mpFrames.size());

		int y1 = 0;
		int y2 = r.bottom;

		if (mSelectionRect.right > mSelectionRect.left) {
			SetBkColor(hdc, RGB(0, 80, 192));

			ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &mSelectionRect, _T(""), 0, nullptr);
		}

		if (mHoverRect.right > mHoverRect.left) {
			SetBkColor(hdc, RGB(128, 0, 0));

			ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &mHoverRect, _T(""), 0, nullptr);
		}

		SetBkMode(hdc, OPAQUE);

		DWORD lastColor = (DWORD)0 - 1;
		float vscale = (float)(y2 - y1) / (float)mVerticalRange;
		for(sint32 frame = frame1; frame < frame2; ++frame) {
			const auto& frameInfo = *mpSession->mpFrames[frame];
			RECT rFill = { frame * mPixelsPerFrame - mScrollX, y2 - VDRoundToInt(frameInfo.mTotalCycles * vscale), (frame + 1) * mPixelsPerFrame - mScrollX, y2 };

			if (mPixelsPerFrame > 4)
				--rFill.right;

			DWORD color = RGB(80, 80, 80);

			if (frame == mHoverPos)
				color = RGB(224, 80, 80);
			else if (frame >= mHotSelectionStart && frame < mHotSelectionEnd)
				color = RGB(144, 192, 240);

			if (lastColor != color) {
				lastColor = color;
				SetBkColor(hdc, color);
			}

			ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rFill, _T(""), 0, nullptr);
		}
	}

	EndPaint(mhwnd, &ps);
}

void ATUIProfilerTimelineView::RecomputeScrollBar() {
	if (!mhwnd)
		return;

	// set scroll bar parameters
	SCROLLINFO si = {sizeof(SCROLLINFO)};

	si.fMask = SIF_PAGE | SIF_RANGE | SIF_DISABLENOSCROLL;
	si.nPage = mWidth;
	si.nMin = 0;
	si.nMax = mPixelsPerFrame * mTotalFrames;

	SetScrollInfo(mhwnd, SB_HORZ, &si, TRUE);

	OnHScroll(SB_THUMBPOSITION, 0);
}

void ATUIProfilerTimelineView::ScrollTo(sint32 xs) {
	sint32 xmax = mTotalFrames * mPixelsPerFrame;
	if (xs + mWidth > xmax)
		xs = xmax - mWidth;

	if (xs < 0)
		xs = 0;

	SCROLLINFO si = { sizeof(SCROLLINFO), SIF_POS };
	GetScrollInfo(mhwnd, SB_HORZ, &si);

	if (xs != si.nPos) {
		SCROLLINFO si = { sizeof(SCROLLINFO) };
		si.nPos = xs;
		si.fMask = SIF_POS;

		SetScrollInfo(mhwnd, SB_HORZ, &si, TRUE);
	}

	if (mScrollX != xs) {
		sint32 oldScrollX = mScrollX;
		mScrollX = xs;

		sint32 delta = oldScrollX - xs;

		if (mSelectionRect.right > mSelectionRect.left)
			OffsetRect(&mSelectionRect, delta, 0);

		if (mHoverRect.right > mHoverRect.left)
			OffsetRect(&mHoverRect, delta, 0);

		if (abs(delta) >= mWidth) {
			InvalidateRect(mhwnd, NULL, TRUE);
		} else {
			ScrollWindowEx(mhwnd, oldScrollX - xs, 0, NULL, NULL, NULL, NULL, SW_ERASE | SW_INVALIDATE);
		}
	}
}

void ATUIProfilerTimelineView::SetZoomIndex(sint32 index, sint32 focusX) {
	if (index >= (int)vdcountof(kZoomFactors))
		index = (int)vdcountof(kZoomFactors) - 1;

	if (index < 0)
		index = 0;

	if (mZoomIndex == index)
		return;

	mZoomIndex = index;

	sint32 ppf = kZoomFactors[mZoomIndex];

	if (mPixelsPerFrame != ppf) {
		// recompute new scroll pos to keep current mouse position centered
		const sint32 centerX = (focusX >= 0 && focusX < mWidth) ? focusX : (mWidth >> 1);
		const sint32 xs = ((mScrollX + centerX) * ppf + (mPixelsPerFrame >> 1)) / mPixelsPerFrame - centerX;

		mPixelsPerFrame = ppf;

		RecomputeScrollBar();
		ScrollTo(xs);

		mHoverRect = FrameToRect(mHoverPos);
		mSelectionRect = FrameRangeToRect(mHotSelectionStart, mHotSelectionEnd);

		InvalidateRect(mhwnd, NULL, TRUE);
	}
}

void ATUIProfilerTimelineView::SetHoverPosition(sint32 pos) {
	if (mHoverPos != pos) {
		if (mhwnd) {
			RECT r = FrameToRect(pos);

			if (mHoverRect.right > mHoverRect.left)
				InvalidateRect(mhwnd, &mHoverRect, TRUE);

			mHoverRect = r;

			if (r.right > r.left)
				InvalidateRect(mhwnd, &r, TRUE);
		}

		mHoverPos = pos;
	}
}

void ATUIProfilerTimelineView::SetSelectedRange(sint32 selStart, sint32 selEnd, bool notify) {
	if (mHotSelectionStart != selStart || mHotSelectionEnd != selEnd) {
		mHotSelectionStart = selStart;
		mHotSelectionEnd = selEnd;

		RECT newRect = FrameRangeToRect(selStart, selEnd);

		// check for overlap
		if (mSelectionRect.right > mSelectionRect.left
			&& newRect.right > newRect.left
			&& mSelectionRect.left < newRect.right
			&& newRect.left < mSelectionRect.right)
		{
			// overlap -- invalidate XOR
			RECT r1 = mSelectionRect;
			RECT r2 = newRect;

			int xpos[4] = { mSelectionRect.left, mSelectionRect.right, newRect.left, newRect.right };

			std::sort(std::begin(xpos), std::end(xpos));

			r1.left = xpos[0];
			r1.right = xpos[1];
			r2.left = xpos[2];
			r2.right = xpos[3];

			InvalidateRect(mhwnd, &r1, TRUE);
			InvalidateRect(mhwnd, &r2, TRUE);
		} else {
			// no overlap -- invalidate both
			if (mSelectionRect.right > mSelectionRect.left)
				InvalidateRect(mhwnd, &mSelectionRect, TRUE);

			if (newRect.right > newRect.left)
				InvalidateRect(mhwnd, &newRect, TRUE);
		}


		mSelectionRect = newRect;
	}

	if (notify && (mSelectionStart != selStart || mSelectionEnd != selEnd)) {
		mSelectionStart = selStart;
		mSelectionEnd = selEnd;

		if (mpOnRangeSelected)
			mpOnRangeSelected(selStart, selEnd);
	}
}

sint32 ATUIProfilerTimelineView::PointToFrame(sint32 x, sint32 y, bool clamp) const {
	if (!mpSession)
		return -1;

	if (x < 0)
		return clamp ? 0 : -1;

	x = (x + mScrollX) / mPixelsPerFrame;

	int n = (int)mpSession->mpFrames.size();
	if (x >= n)
		return clamp ? n - 1 : -1;

	return x;
}

RECT ATUIProfilerTimelineView::FrameToRect(sint32 pos) const {
	RECT r = {0};

	if (pos >= 0 && pos < (int)mpSession->mpFrames.size()) {
		if (mhwnd && GetClientRect(mhwnd, &r)) {
			r.left = pos * mPixelsPerFrame - mScrollX;
			r.right = r.left + mPixelsPerFrame;
		}
	}

	return r;
}

RECT ATUIProfilerTimelineView::FrameRangeToRect(sint32 start, sint32 end) const {
	RECT r = {0};

	if (start < end && end >= 0 && start < (int)mpSession->mpFrames.size()) {
		if (mhwnd && GetClientRect(mhwnd, &r)) {
			r.left = start * mPixelsPerFrame - mScrollX;
			r.right = end * mPixelsPerFrame - mScrollX;
		}
	}

	return r;
}

/////////////////////////////////////////////////////////////////////////////

class ATUIProfilerSourceTextView final : public ATUINativeWindow {
public:
	ATUIProfilerSourceTextView(const ATProfileFrame& profFrame);
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

ATUIProfilerSourceTextView::ATUIProfilerSourceTextView(const ATProfileFrame& profFrame)
	: mRecords(profFrame.mRecords)
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

	std::advance(itLines, std::min<int>((int)mLines.size(), kScrollMarginLines));

	ATCPUHistoryEntry hent;
	ATDisassembleCaptureRegisterContext(hent);

	const ATDebugDisasmMode disasmMode = g_sim.GetCPU().GetDisasmMode();
	IATDebugTarget *target = g_sim.GetDebugTarget();

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

			ATDisassembleInsn(mBuffer, target, disasmMode, hent, false, false, true, true, true);

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
			newPos = mLines[std::min<int>((int)mLines.size(), kScrollMarginLines + 1) - 1];
			break;
		case SB_PAGEDOWN:
			newPos = mLines[std::max<int>((int)mLines.size(), kScrollMarginLines + 1) - (kScrollMarginLines + 1)];
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

class ATUIProfilerSourcePane final : public ATUINativeWindow {
public:
	ATUIProfilerSourcePane(const ATProfileFrame& frame, uint32 baseAddr, vdrefcount *frameOwner);
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
	HFONT mhPropFont;
	vdrefptr<ATUIProfilerSourceTextView> mpSourceView;

	const ATProfileFrame& mFrame;
	vdrefptr<vdrefcount> mpFrameOwner;
};

ATUIProfilerSourcePane::ATUIProfilerSourcePane(const ATProfileFrame& frame, uint32 baseAddr, vdrefcount *frameOwner)
	: mhwndHeader(NULL)
	, mBaseAddress(baseAddr)
	, mFrame(frame)
	, mpFrameOwner(frameOwner)
{
}

ATUIProfilerSourcePane::~ATUIProfilerSourcePane() {
}

bool ATUIProfilerSourcePane::Create(HWND hwndParent) {
	if (!CreateWindow(MAKEINTATOM(sWndClass), _T("Altirra Profiler - Detailed View"), WS_VISIBLE|WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndParent, NULL, VDGetLocalModuleHandleW32(), static_cast<ATUINativeWindow *>(this)))
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

						if ((hdr2.pitem->mask & HDI_WIDTH) && hdr2.iItem < 5)
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
	mhPropFont = ATUICreateDefaultFontForDpiW32(ATUIGetWindowDpiW32(mhwnd));

	SendMessage(mhwndHeader, WM_SETFONT, (WPARAM)mhPropFont, TRUE);

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

	mpSourceView = new ATUIProfilerSourceTextView(mFrame);
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

	if (mhPropFont) {
		DeleteObject(mhPropFont);
		mhPropFont = nullptr;
	}
}

void ATUIProfilerSourcePane::OnSize() {
	RECT r;
	GetClientRect(mhwnd, &r);

	WINDOWPOS wph;
	HDLAYOUT hdl = {&r, &wph};
	SendMessage(mhwndHeader, HDM_LAYOUT, 0, (LPARAM)&hdl);

	SetWindowPos(mhwndHeader, NULL, wph.x, wph.y, wph.cx, wph.cy, SWP_NOZORDER | SWP_NOACTIVATE);

	const int count = (int)SendMessage(mhwndHeader, HDM_GETITEMCOUNT, 0, 0);
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

class ATUIDialogProfilerBoundaryRule : public VDDialogFrameW32 {
public:
	ATUIDialogProfilerBoundaryRule();

	void SetValues(const char *s, const char *t, bool endFunction) { mExpr = s; mExpr2 = t; mbEndFunction = endFunction; }
	const char *GetExpression() const { return mExpr.c_str(); }
	const char *GetExpression2() const { return mExpr2.c_str(); }
	bool IsEndFunctionEnabled() const { return mbEndFunction; }

private:
	void OnDataExchange(bool write) override;

	VDStringA mExpr;
	VDStringA mExpr2;
	bool mbEndFunction;
};

ATUIDialogProfilerBoundaryRule::ATUIDialogProfilerBoundaryRule()
	: VDDialogFrameW32(IDD_PROFILER_BOUNDARYRULE)
{
}

void ATUIDialogProfilerBoundaryRule::OnDataExchange(bool write) {
	if (write) {
		VDStringW sw;
		VDStringW tw;
		if (!GetControlText(IDC_ADDRESS, sw)) {
			FailValidation(IDC_ADDRESS);
			return;
		}
		if (!GetControlText(IDC_ADDRESS2, tw)) {
			FailValidation(IDC_ADDRESS2);
			return;
		}

		VDStringA s = VDTextWToA(sw);
		VDStringA t = VDTextWToA(tw);

		try {
			ATGetDebugger()->EvaluateThrow(s.c_str());
		} catch(const MyError& e) {
			FailValidation(IDC_ADDRESS, VDTextAToW(e.c_str()).c_str());
			return;
		}

		bool endFunction = IsButtonChecked(IDC_ENDFUNCTION);

		if (endFunction) {
			t.clear();
		} else if (!t.empty()) {
			try {
				ATGetDebugger()->EvaluateThrow(t.c_str());
			} catch(const MyError& e) {
				FailValidation(IDC_ADDRESS2, VDTextAToW(e.c_str()).c_str());
				return;
			}
		}

		mExpr.swap(s);
		mExpr2.swap(t);
		mbEndFunction = endFunction;
	} else {
		SetControlText(IDC_ADDRESS, VDTextAToW(mExpr).c_str());
		SetControlText(IDC_ADDRESS2, VDTextAToW(mExpr2).c_str());
		CheckButton(IDC_ENDFUNCTION, mbEndFunction);
	}
}

/////////////////////////////////////////////////////////////////////////////

class ATUIProfilerPane final : public ATUIPane {
public:
	ATUIProfilerPane();
	~ATUIProfilerPane();

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnFontsUpdated();
	bool OnCommand(uint32 id, uint32 extcode);
	bool OnNotify(uint32 id, uint32 code, const void *hdr, LRESULT& result);
	void OnColumnClicked(VDUIProxyListView *lv, int column);
	void OnItemDoubleClicked(VDUIProxyListView *lv, int item);
	void OnItemSelectionChanged(VDUIProxyListView *lv, int item);
	void OnItemContextMenu(VDUIProxyListView *lv, VDUIProxyListView::ContextMenuEvent event);
	void OnTreeItemDoubleClicked(VDUIProxyTreeViewControl *tv, bool *handled);
	void OnRangeSelected(uint32 start, uint32 end);
	void UpdateRunButtonEnables();
	void RebuildToolbar();
	void UpdateProfilingModeBitmap();

	void UnloadProfile();
	void LoadProfile();
	void RemakeView();
	void MergeFrames(uint32 start, uint32 end);

	void CopyAsCsv();

	void StartProfiler();

	void VLGetText(int item, int subItem, VDStringW& s) const;
	void VTGetText(int item, VDStringW& s) const;

	struct MergedProfileFrame : public vdrefcount, public ATProfileFrame {
		vdfastvector<ATProfileCallGraphInclusiveRecord> mInclusiveRecords;
	};

	HWND mhwndToolbar;
	HWND mhwndList;
	HWND mhwndTree;
	HWND mhwndStatus;
	HWND mhwndMessage;
	HFONT mhPropFont;
	HMENU mhmenuMode;
	HIMAGELIST mToolbarImageList;
	ATProfileMode mProfileMode = kATProfileMode_Insns;
	ATProfileMode mCapturedProfileMode = kATProfileMode_Insns;
	ATProfileCounterMode mProfileCounterModes[2];
	ATProfileCounterMode mProfileSessionCounterModes[2];
	ATProfileBoundaryRule mBoundaryRule = kATProfileBoundaryRule_None;
	VDStringA mBoundaryAddrExpr;
	VDStringA mBoundaryAddrExpr2;

	ATProfileSession mSession;
	vdrefptr<MergedProfileFrame> mpMergedFrame;
	const ATProfileFrame *mpCurrentFrame = nullptr;
	const ATProfileFrame::Records *mpRecords = nullptr;

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
			mSort[7] = 7;
			mSort[8] = 8;
			mSort[9] = 9;
			mSort[10] = 10;
			mDescending[0] = 0;
			mDescending[1] = 0;
			mDescending[2] = -1;
			mDescending[3] = -1;
			mDescending[4] = -1;
			mDescending[5] = -1;
			mDescending[6] = -1;
			mDescending[7] = -1;
			mDescending[8] = -1;
			mDescending[9] = -1;
			mDescending[10] = -1;
		}

		int Compare(IVDUIListViewVirtualItem *x, IVDUIListViewVirtualItem *y) {
			VLItem& a = *static_cast<VLItem *>(x);
			VLItem& b = *static_cast<VLItem *>(y);

			const ATProfileRecord& r = (*a.mpParent->mpRecords)[a.mIndex];
			const ATProfileRecord& s = (*a.mpParent->mpRecords)[b.mIndex];

			for(int i=0; i<(int)vdcountof(mSort); ++i) {
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

					case 7:
						diff = (((int)r.mCounters[0] - (int)s.mCounters[0]) ^ inv) - inv;
						break;

					case 8:
						{
							uint64 a = (uint64)r.mCounters[0] * s.mInsns;
							uint64 b = (uint64)s.mCounters[0] * r.mInsns;

							diff = (a < b) ? -1 : (a > b) ? +1 : 0;
							diff = (diff ^ inv) - inv;
						}
						break;

					case 9:
						diff = (((int)r.mCounters[1] - (int)s.mCounters[1]) ^ inv) - inv;
						break;

					case 10:
						{
							uint64 a = (uint64)r.mCounters[1] * s.mInsns;
							uint64 b = (uint64)s.mCounters[1] * r.mInsns;

							diff = (a < b) ? -1 : (a > b) ? +1 : 0;
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

			for(int i=1; i<(int)vdcountof(mSort); ++i) {
				if (mSort[i] == idx) {
					memmove(mSort + 1, mSort, i);
					mSort[0] = idx;
					break;
				}
			}
		}

		uint8 mSort[11];
		sint8 mDescending[11];
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
	vdvector<VDStringW> mListColumnNames;

	VDDelegate mDelegateColumnClicked;
	VDDelegate mDelegateItemDoubleClicked;
	VDDelegate mDelegateItemSelectionChanged;
	VDDelegate mDelegateItemContextMenu;
	VDDelegate mDelegateTreeItemDoubleClicked;

	ATUIProfilerTimelineView mTimelineView;

	static const UINT kCounterModeMenuIds[];
	static const wchar_t *const kCounterModeColumnNames[];
};

const UINT ATUIProfilerPane::kCounterModeMenuIds[] = {
	ID_COUNTER_BRANCHTAKEN,
	ID_COUNTER_BRANCHNOTTAKEN,
	ID_COUNTER_PAGECROSSING,
	ID_COUNTER_REDUNDANTOPERATION,
};

const wchar_t *const ATUIProfilerPane::kCounterModeColumnNames[] = {
	L"Taken",
	L"NotTaken",
	L"PageCross",
	L"Redundant",
};

ATUIProfilerPane::ATUIProfilerPane()
	: ATUIPane(kATUIPaneId_Profiler, L"Profile View")
	, mhwndToolbar(NULL)
	, mhwndList(NULL)
	, mhwndTree(NULL)
	, mhwndStatus(NULL)
	, mhmenuMode(NULL)
	, mhPropFont(nullptr)
	, mToolbarImageList(NULL)
{
	for(auto& v : mProfileCounterModes)
		v = kATProfileCounterMode_None;

	for(auto& v : mProfileSessionCounterModes)
		v = kATProfileCounterMode_None;

	mListView.OnColumnClicked() += mDelegateColumnClicked.Bind(this, &ATUIProfilerPane::OnColumnClicked);
	mListView.OnItemDoubleClicked() += mDelegateItemDoubleClicked.Bind(this, &ATUIProfilerPane::OnItemDoubleClicked);
	mListView.OnItemSelectionChanged() += mDelegateItemSelectionChanged.Bind(this, &ATUIProfilerPane::OnItemSelectionChanged);
	mListView.OnItemContextMenu() += mDelegateItemContextMenu.Bind(this, &ATUIProfilerPane::OnItemContextMenu);
	mTreeView.OnItemDoubleClicked() += mDelegateTreeItemDoubleClicked.Bind(this, &ATUIProfilerPane::OnTreeItemDoubleClicked);

	// pin embedded timeline view object
	mTimelineView.AddRef();
	mTimelineView.SetOnRangeSelected([this](uint32 start, uint32 end) { OnRangeSelected(start, end); });
}

ATUIProfilerPane::~ATUIProfilerPane() {
}

LRESULT ATUIProfilerPane::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_COMMAND) {
		if (OnCommand(LOWORD(wParam), HIWORD(wParam)))
			return 0;

		return mDispatcher.Dispatch_WM_COMMAND(wParam, lParam);
	} else if (msg == WM_ERASEBKGND) {
		RECT r;

		if (GetClientRect(mhwnd, &r)) {
			FillRect((HDC)wParam, &r, (HBRUSH)(COLOR_3DFACE + 1));
			return TRUE;
		}
	} else if (msg == WM_NOTIFY) {
		const NMHDR& hdr = *(const NMHDR *)lParam;
		LRESULT res = 0;

		if (OnNotify((uint32)hdr.idFrom, hdr.code, &hdr, res))
			return res;

		return mDispatcher.Dispatch_WM_NOTIFY(wParam, lParam);
	} else if (msg == WM_CONTEXTMENU) {
		if (mListView.IsVisible()) {
			POINT pt { 0, 0 };
			ClientToScreen(mListView.GetHandle(), &pt);
			OnItemContextMenu(&mListView, {0, pt.x, pt.y});
		}
	} else if (msg == WM_USER + 100) {
		const VTItem *vti = static_cast<VTItem *>(mTreeView.GetSelectedVirtualItem());

		if (vti) {
			int idx = vti->mIndex;

			vdrefptr<ATUIProfilerSourcePane> srcPane(new ATUIProfilerSourcePane(*mpCurrentFrame, mSession.mContexts[idx].mAddress, mpMergedFrame));
			srcPane->Create(mhwnd);
		}

		return 0;
	}

	return ATUIPane::WndProc(msg, wParam, lParam);
}

bool ATUIProfilerPane::OnCreate() {
	mhmenuMode = LoadMenu(NULL, MAKEINTRESOURCE(IDR_PROFILE_MODE_MENU));
	mhPropFont = ATUICreateDefaultFontForDpiW32(ATUIGetWindowDpiW32(mhwnd));

	mToolbarImageList = ImageList_LoadBitmap(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDB_TOOLBAR_PROFILER), 16, 0, RGB(255, 0, 255));

	mhwndToolbar = CreateWindow(TOOLBARCLASSNAME, _T(""), WS_CHILD | WS_VISIBLE | TBSTYLE_LIST, 0, 0, 0, 0, mhwnd, (HMENU)100, VDGetLocalModuleHandleW32(), NULL);
	if (!mhwndToolbar)
		return false;

	mhwndList = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_NOPARENTNOTIFY, WC_LISTVIEW, _T(""), WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SHOWSELALWAYS, 0, 0, 0, 0, mhwnd, (HMENU)101, VDGetLocalModuleHandleW32(), NULL);
	SendMessage(mhwndList, WM_SETFONT, (WPARAM)mhPropFont, TRUE);

	mhwndTree = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_NOPARENTNOTIFY, WC_TREEVIEW, _T(""), WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TVS_FULLROWSELECT | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 0, 0, mhwnd, (HMENU)103, VDGetLocalModuleHandleW32(), NULL);
	SendMessage(mhwndTree, WM_SETFONT, (WPARAM)mhPropFont, TRUE);

	mhwndStatus = CreateWindowEx(0, STATUSCLASSNAME, _T(""), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, mhwnd, (HMENU)102, VDGetLocalModuleHandleW32(), NULL);
	SendMessage(mhwndStatus, WM_SETFONT, (WPARAM)mhPropFont, TRUE);
	SendMessage(mhwndStatus, SB_SIMPLE, 0, 0);
	SetWindowPos(mhwndStatus, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	
	SendMessage(mhwndToolbar, WM_SETFONT, (WPARAM)mhPropFont, TRUE);
	SendMessage(mhwndToolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS);
	SendMessage(mhwndToolbar, TB_SETIMAGELIST, 0, (LPARAM)mToolbarImageList);
	SendMessage(mhwndToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
	
	mhwndMessage = CreateWindowEx(0, WC_STATIC, _T(""), WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, mhwnd, (HMENU)104, VDGetLocalModuleHandleW32(), NULL);
	SendMessage(mhwndMessage, WM_SETFONT, (WPARAM)mhPropFont, TRUE);

	mTimelineView.CreateChild(mhwnd, 105, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS);

	RebuildToolbar();

	mListView.Attach(mhwndList);
	mDispatcher.AddControl(&mListView);

	mListView.SetFullRowSelectEnabled(true);
	mListView.InsertColumn(0, L"Thread", 0);
	mListView.AutoSizeColumns();

	mTreeView.Attach(mhwndTree);
	mDispatcher.AddControl(&mTreeView);

	UpdateRunButtonEnables();
	RemakeView();

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

	if (mhwndMessage) {
		::DestroyWindow(mhwndMessage);
		mhwndMessage = nullptr;
	}

	if (mToolbarImageList) {
		ImageList_Destroy(mToolbarImageList);
		mToolbarImageList = NULL;
	}

	if (mhPropFont) {
		DeleteObject(mhPropFont);
		mhPropFont = nullptr;
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
		SendMessage(mhwndStatus, WM_SIZE, 0, 0);

		RECT rs;
		if (GetWindowRect(mhwndStatus, &rs)) {
			int statusH = rs.bottom - rs.top;
			statusY = std::max<int>(0, r.bottom - statusH);
		}
	}

	int lh = statusY - y;

	if (lh < 0)
		lh = 0;

	int toph = 0;

	if (HWND hwndTimelineView = mTimelineView.GetHandleW32()) {
		if (mSession.mpFrames.size() > 1) {
			toph = lh / 3;
			SetWindowPos(hwndTimelineView, NULL, 0, y, r.right, toph, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
		} else {
			ShowWindow(hwndTimelineView, SW_HIDE);
		}
	}

	int both = lh - toph;

	if (mhwndList)
		SetWindowPos(mhwndList, NULL, 0, y + toph, r.right, both, SWP_NOZORDER | SWP_NOACTIVATE);

	if (mhwndTree)
		SetWindowPos(mhwndTree, NULL, 0, y + toph, r.right, both, SWP_NOZORDER | SWP_NOACTIVATE);

	if (mhwndMessage) {
		int edge = GetSystemMetrics(SM_CXEDGE) * 2;

		SetWindowPos(mhwndMessage, NULL, edge, y+edge, std::max<int>(0, r.right - 2*edge), std::max<int>(0, lh - 2*edge), SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
	}
}

void ATUIProfilerPane::OnFontsUpdated() {
	HFONT hNewFont = ATUICreateDefaultFontForDpiW32(ATUIGetWindowDpiW32(mhwnd));

	if (mhwndList)
		SendMessage(mhwndList, WM_SETFONT, (WPARAM)hNewFont, TRUE);

	if (mhwndTree)
		SendMessage(mhwndTree, WM_SETFONT, (WPARAM)hNewFont, TRUE);

	if (mhwndStatus)
		SendMessage(mhwndStatus, WM_SETFONT, (WPARAM)hNewFont, TRUE);

	if (mhwndToolbar) {
		SendMessage(mhwndToolbar, WM_SETFONT, (WPARAM)hNewFont, TRUE);
		
		RebuildToolbar();
	}

	if (mhwndMessage)
		SendMessage(mhwndMessage, WM_SETFONT, (WPARAM)hNewFont, TRUE);

	if (mhPropFont)
		::DeleteObject(mhPropFont);

	mhPropFont = hNewFont;
}

bool ATUIProfilerPane::OnCommand(uint32 id, uint32 extcode) {
	try {
		switch(id) {
			case 1000:
				ATGetDebugger()->Break();
			
				if (ATCPUProfiler *prof = g_sim.GetProfiler()) {
					prof->End();
					LoadProfile();
					g_sim.SetProfilingEnabled(false);
				}
				UpdateRunButtonEnables();
				return true;

			case 1004:
				StartProfiler();
				UnloadProfile();
				ATGetDebugger()->Break();
				UpdateRunButtonEnables();
				return true;

			case 1001:
				StartProfiler();
				UnloadProfile();
				ATGetDebugger()->Run(kATDebugSrcMode_Same);
				UpdateRunButtonEnables();
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

			case 1003:
				{
					RECT r;
					SendMessage(mhwndToolbar, TB_GETRECT, 1003, (LPARAM)&r);
					MapWindowPoints(mhwndToolbar, NULL, (LPPOINT)&r, 2);
					HMENU hmenu = LoadMenu(NULL, MAKEINTRESOURCE(IDR_PROFILE_OPTIONS_MENU));

					if (!hmenu)
						return true;

					uint32 activeMask = 0;

					for(const auto cm : mProfileCounterModes) {
						if (cm) {
							activeMask |= (1 << (cm - 1));
							VDCheckMenuItemByCommandW32(hmenu, kCounterModeMenuIds[cm - 1], true);
						}
					}

					if (mProfileCounterModes[vdcountof(mProfileCounterModes) - 1]) {
						for(uint32 i=0; i<vdcountof(kCounterModeMenuIds); ++i) {
							if (!(activeMask & (1 << i)))
								VDEnableMenuItemByCommandW32(hmenu, kCounterModeMenuIds[i], false);
						}
					}

					switch(mBoundaryRule) {
						case kATProfileBoundaryRule_None:
							VDCheckRadioMenuItemByCommandW32(hmenu, ID_FRAMETRIGGER_NONE, true);
							break;

						case kATProfileBoundaryRule_VBlank:
							VDCheckRadioMenuItemByCommandW32(hmenu, ID_FRAMETRIGGER_VBLANK, true);
							break;

						case kATProfileBoundaryRule_PCAddress:
							VDCheckRadioMenuItemByCommandW32(hmenu, ID_FRAMETRIGGER_PCADDRESS, true);
							break;
					}

					TPMPARAMS tpm;
					tpm.cbSize = sizeof(TPMPARAMS);
					tpm.rcExclude = r;
					UINT selectedId = (UINT)TrackPopupMenuEx(GetSubMenu(hmenu, 0), TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_VERTICAL | TPM_NONOTIFY | TPM_RETURNCMD, r.left, r.bottom, mhwnd, &tpm);

					for(uint32 i=0; i<vdcountof(kCounterModeMenuIds); ++i) {
						if (selectedId == kCounterModeMenuIds[i]) {
							ATProfileCounterMode selectedMode = (ATProfileCounterMode)(i + 1);

							for(auto& cm : mProfileCounterModes) {
								if (cm == selectedMode) {
									cm = kATProfileCounterMode_None;
									selectedMode = kATProfileCounterMode_None;
									break;
								}
							}

							if (selectedMode)
								mProfileCounterModes[vdcountof(mProfileCounterModes) - 1] = selectedMode;

							std::sort(std::begin(mProfileCounterModes), std::end(mProfileCounterModes));

							auto *dst = mProfileCounterModes;
							for(auto cm : mProfileCounterModes) {
								if (cm)
									*dst++ = cm;
							}

							while(dst != std::end(mProfileCounterModes))
								*dst++ = kATProfileCounterMode_None;

							break;
						}
					}

					if (selectedId == ID_FRAMETRIGGER_NONE) {
						mBoundaryRule = kATProfileBoundaryRule_None;
					} else if (selectedId == ID_FRAMETRIGGER_VBLANK) {
						mBoundaryRule = kATProfileBoundaryRule_VBlank;
					} else if (selectedId == ID_FRAMETRIGGER_PCADDRESS) {
						ATUIDialogProfilerBoundaryRule dlg;

						dlg.SetValues(mBoundaryAddrExpr.c_str(), mBoundaryAddrExpr2.c_str(), mBoundaryRule == kATProfileBoundaryRule_PCAddressFunction);
						if (dlg.ShowDialog((VDGUIHandle)mhwnd)) {
							mBoundaryAddrExpr = dlg.GetExpression();

							if (dlg.IsEndFunctionEnabled()) {
								mBoundaryAddrExpr2.clear();
								mBoundaryRule = kATProfileBoundaryRule_PCAddressFunction;
							} else {
								mBoundaryAddrExpr2 = dlg.GetExpression2();
								mBoundaryRule = kATProfileBoundaryRule_PCAddress;
							}
						}
					}

					DestroyMenu(hmenu);
				}
				return true;

			case ID_PROFMODE_SAMPLEINSNS:
				SendMessage(mhwndToolbar, TB_CHANGEBITMAP, 1002, 2);
				mProfileMode = kATProfileMode_Insns;
				UpdateProfilingModeBitmap();
				return true;

			case ID_PROFMODE_SAMPLEFNS:
				SendMessage(mhwndToolbar, TB_CHANGEBITMAP, 1002, 3);
				mProfileMode = kATProfileMode_Functions;
				UpdateProfilingModeBitmap();
				return true;

			case ID_PROFMODE_CALLGRAPH:
				SendMessage(mhwndToolbar, TB_CHANGEBITMAP, 1002, 5);
				mProfileMode = kATProfileMode_CallGraph;
				UpdateProfilingModeBitmap();
				return true;

			case ID_PROFMODE_BASICBLOCK:
				SendMessage(mhwndToolbar, TB_CHANGEBITMAP, 1002, 6);
				mProfileMode = kATProfileMode_BasicBlock;
				UpdateProfilingModeBitmap();
				return true;

			case ID_PROFMODE_SAMPLEBASIC:
				SendMessage(mhwndToolbar, TB_CHANGEBITMAP, 1002, 4);
				mProfileMode = kATProfileMode_BasicLines;
				UpdateProfilingModeBitmap();
				return true;
		}
	} catch(const MyError& e) {
		e.post(mhwnd, "Altirra Error");
	}

	return false;
}

bool ATUIProfilerPane::OnNotify(uint32 id, uint32 code, const void *hdr, LRESULT& result) {
	switch(id) {
		case 100:
			if (code == TBN_DROPDOWN) {
				const NMTOOLBAR& tbhdr = *(const NMTOOLBAR *)hdr;

				if (tbhdr.iItem == 1002 || tbhdr.iItem == 1003) {
					OnCommand(tbhdr.iItem, 0);
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
		0, 1, 2, 3, 4, 3, 4, 5, 5, 6, 7, 8, 9, 10
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
		const auto& record = (*mpRecords)[idx];

		if (record.mAddress == 0xFFFFFF) {
			::MessageBoxW(mhwnd, L"The selected entry corresponds to the unknown function executing when profiling was started. It can't be expanded.", L"Altirra Error", MB_ICONEXCLAMATION | MB_OK);
		} else {
			vdrefptr<ATUIProfilerSourcePane> srcPane(new ATUIProfilerSourcePane(*mpCurrentFrame, (*mpRecords)[idx].mAddress, mpMergedFrame));
			srcPane->Create(mhwnd);
		}
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
	uint32 count = (uint32)selectedIndices.size();
	while(!selectedIndices.empty()) {
		int lvIndex = selectedIndices.back();
		selectedIndices.pop_back();

		VLItem *vli = static_cast<VLItem *>(lv->GetVirtualItem(lvIndex));

		if (!vli)
			continue;

		const ATProfileRecord& rec = (*mpRecords)[vli->mIndex];
		insns += rec.mInsns;
		cycles += rec.mCycles;
	}

	VDSetWindowTextFW32(mhwndStatus
		, L"Selected %u item%ls: %u cycles (%.2f%%), %u insns (%.2f%%)"
		, count
		, count == 1 ? L"" : L"s"
		, cycles
		, mpCurrentFrame->mTotalCycles ? (float)cycles * 100.0f / (float)mpCurrentFrame->mTotalCycles : 0
		, insns
		, mpCurrentFrame->mTotalInsns ? (float)insns * 100.0f / (float)mpCurrentFrame->mTotalInsns : 0
		);
}

void ATUIProfilerPane::OnItemContextMenu(VDUIProxyListView *lv, VDUIProxyListView::ContextMenuEvent event) {
	HMENU hmenu = LoadMenu(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDR_PROFILE_LIST_CONTEXT_MENU));
	if (!hmenu)
		return;

	UINT selectedId = (UINT)TrackPopupMenuEx(GetSubMenu(hmenu, 0), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_HORIZONTAL | TPM_NONOTIFY | TPM_RETURNCMD, event.mX, event.mY, mhwnd, NULL);

	DestroyMenu(hmenu);

	switch(selectedId) {
		case ID_COPYASCSV:
			CopyAsCsv();
			break;
	}
}

void ATUIProfilerPane::OnTreeItemDoubleClicked(VDUIProxyTreeViewControl *tv, bool *handled) {
	*handled = true;

	// We can't open the window here as the tree view steals focus afterward.
	::PostMessage(mhwnd, WM_USER + 100, 0, 0);
}

void ATUIProfilerPane::OnRangeSelected(uint32 start, uint32 end) {
	MergeFrames(start, end);
}

void ATUIProfilerPane::UpdateRunButtonEnables() {
	ATCPUProfiler *profiler = g_sim.GetProfiler();
	const bool enabled = profiler && profiler->IsRunning();
	const bool running = g_sim.IsRunning();

	SendMessage(mhwndToolbar, TB_ENABLEBUTTON, 1000, enabled);
	SendMessage(mhwndToolbar, TB_ENABLEBUTTON, 1004, !enabled || running);
	SendMessage(mhwndToolbar, TB_ENABLEBUTTON, 1001, !enabled || !running);
}

void ATUIProfilerPane::RebuildToolbar() {
	while(SendMessage(mhwndToolbar, TB_DELETEBUTTON, 0, 0))
		;

	TBBUTTON tbb[5] = {0};
	tbb[0].iBitmap = 0;
	tbb[0].idCommand = 1000;
	tbb[0].fsState = TBSTATE_ENABLED;
	tbb[0].fsStyle = TBSTYLE_BUTTON | BTNS_AUTOSIZE;
	tbb[0].dwData = 0;
	tbb[0].iString = 0;

	tbb[1].iBitmap = 7;
	tbb[1].idCommand = 1004;
	tbb[1].fsState = TBSTATE_ENABLED;
	tbb[1].fsStyle = TBSTYLE_BUTTON | BTNS_AUTOSIZE;
	tbb[1].dwData = 0;
	tbb[1].iString = 0;

	tbb[2].iBitmap = 1;
	tbb[2].idCommand = 1001;
	tbb[2].fsState = TBSTATE_ENABLED;
	tbb[2].fsStyle = TBSTYLE_BUTTON | BTNS_AUTOSIZE;
	tbb[2].dwData = 0;
	tbb[2].iString = 0;

	tbb[3].iBitmap = 2;
	tbb[3].idCommand = 1002;
	tbb[3].fsState = TBSTATE_ENABLED;
	tbb[3].fsStyle = BTNS_WHOLEDROPDOWN | BTNS_AUTOSIZE;
	tbb[3].dwData = 0;
	tbb[3].iString = 0;

	tbb[4].iBitmap = -2;
	tbb[4].idCommand = 1003;
	tbb[4].fsState = TBSTATE_ENABLED;
	tbb[4].fsStyle = BTNS_WHOLEDROPDOWN | BTNS_AUTOSIZE;
	tbb[4].dwData = 0;
	tbb[4].iString = (INT_PTR)L"Options";

	SendMessage(mhwndToolbar, TB_ADDBUTTONS, 5, (LPARAM)tbb);
	SendMessage(mhwndToolbar, TB_AUTOSIZE, 0, 0);

	UpdateRunButtonEnables();
	UpdateProfilingModeBitmap();
}

void ATUIProfilerPane::UpdateProfilingModeBitmap() {
	if (!mhwndToolbar)
		return;

	switch(mProfileMode) {
		case kATProfileMode_Insns:
		default:
			SendMessage(mhwndToolbar, TB_CHANGEBITMAP, 1002, 2);
			break;

		case kATProfileMode_Functions:
			SendMessage(mhwndToolbar, TB_CHANGEBITMAP, 1002, 3);
			break;

		case kATProfileMode_CallGraph:
			SendMessage(mhwndToolbar, TB_CHANGEBITMAP, 1002, 5);
			break;

		case kATProfileMode_BasicBlock:
			SendMessage(mhwndToolbar, TB_CHANGEBITMAP, 1002, 6);
			break;

		case kATProfileMode_BasicLines:
			SendMessage(mhwndToolbar, TB_CHANGEBITMAP, 1002, 4);
			break;
	}
}

void ATUIProfilerPane::UnloadProfile() {
	mSession = decltype(mSession)();
	mpCurrentFrame = nullptr;

	mTimelineView.SetSession(nullptr);

	OnSize();
	RemakeView();
}

void ATUIProfilerPane::LoadProfile() {
	g_sim.GetProfiler()->GetSession(mSession);

	MergeFrames(0, 1);

	memcpy(mProfileSessionCounterModes, mProfileCounterModes, sizeof mProfileSessionCounterModes);
	
	mTimelineView.SetSession(&mSession);
	mTimelineView.SetSelectedRange(0, 1);

	// compute truncated mean to set half point vertically
	size_t n = mSession.mpFrames.size();
	vdfastvector<uint32> frameDurations(n);

	std::transform(mSession.mpFrames.begin(), mSession.mpFrames.end(), frameDurations.begin(),
		[](const ATProfileFrame *frame) { return frame->mTotalCycles; });

	std::sort(frameDurations.begin(), frameDurations.end());

	size_t n4 = n / 4;
	size_t n2 = n - n4*2;

	uint32_t vrange = (uint32_t)((std::accumulate(frameDurations.begin() + n4, frameDurations.end() - n4, (uint64_t)0) * 2 + (n2 / 2)) / n2);

	mTimelineView.SetVerticalRange(vrange);

	OnSize();
	RemakeView();
}

namespace {
	struct CallGraphRecordSorter {
		CallGraphRecordSorter(const ATProfileCallGraphInclusiveRecord *records)
			: mpRecords(records) {}

		bool operator()(uint32 idx1, uint32 idx2) const {
			return mpRecords[idx1].mInclusiveCycles > mpRecords[idx2].mInclusiveCycles;
		}

		const ATProfileCallGraphInclusiveRecord *const mpRecords;
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

	mpRecords = nullptr;

	if (mpCurrentFrame) {
		switch(mCapturedProfileMode) {
			case kATProfileMode_Insns:
			case kATProfileMode_BasicLines:
			case kATProfileMode_CallGraph:
				mpRecords = &mpCurrentFrame->mRecords;
				break;

			default:
				mpRecords = &mpCurrentFrame->mBlockRecords;
				break;
		}
	}

	mListColumnNames.clear();

	if (!mpRecords || mpRecords->empty()) {
		::SetWindowText(mhwndMessage, _T("No profiling data is available. Begin execution with the Play button in this profiler pane to begin data collection and Stop to end the session."));

		::ShowWindow(mhwndMessage, SW_SHOWNOACTIVATE);
		::ShowWindow(mhwndList, SW_HIDE);
		::ShowWindow(mhwndTree, SW_HIDE);
		::ShowWindow(mTimelineView.GetHandleW32(), SW_HIDE);
	} else {
		::ShowWindow(mhwndMessage, SW_HIDE);
		::ShowWindow(mTimelineView.GetHandleW32(), SW_SHOW);

		if (mCapturedProfileMode == kATProfileMode_CallGraph) {
			::ShowWindow(mhwndList, SW_HIDE);
			::ShowWindow(mhwndTree, SW_SHOWNOACTIVATE);

			const uint32 n = (uint32)mpCurrentFrame->mCallGraphRecords.size();

			VDStringW s;
			vdfastvector<VDUIProxyTreeViewControl::NodeRef> nodes(n, VDUIProxyTreeViewControl::kNodeRoot);
			vdfastvector<uint32> nextSibling(n, 0);
			vdfastvector<uint32> firstChild(n, 0);
			for(uint32 i = 4; i < n; ++i) {
				if (!mpMergedFrame->mInclusiveRecords[i].mInclusiveInsns)
					continue;

				uint32 parent = mSession.mContexts[i].mParent;

				nextSibling[i] = firstChild[parent];
				firstChild[parent] = i;
			}

			vdfastvector<uint32> stack(4);
			stack[0] = 0;
			stack[1] = 1;
			stack[2] = 2;
			stack[3] = 3;
			std::sort(stack.begin(), stack.end(), CallGraphRecordSorter(mpMergedFrame->mInclusiveRecords.data()));

			mVTItems.resize(n);

			while(!stack.empty()) {
				uint32 i = stack.back();
				stack.pop_back();

				const ATProfileCallGraphRecord& cgr = mpCurrentFrame->mCallGraphRecords[i];

				VTItem *pvi = &mVTItems[i];
				pvi->Init(i, this);
				nodes[i] = mTreeView.AddVirtualItem(i < 4 ? VDUIProxyTreeViewControl::kNodeRoot : nodes[mSession.mContexts[i].mParent], VDUIProxyTreeViewControl::kNodeFirst, pvi);

				uint32 childBase = (uint32)stack.size();

				for(uint32 j = firstChild[i]; j; j = nextSibling[j])
					stack.push_back(j);

				std::sort(stack.begin() + childBase, stack.end(), CallGraphRecordSorter(mpMergedFrame->mInclusiveRecords.data()));
			}
		} else {
			::ShowWindow(mhwndList, SW_SHOWNOACTIVATE);
			::ShowWindow(mhwndTree, SW_HIDE);

			switch(mCapturedProfileMode) {
				case kATProfileMode_Insns:
				case kATProfileMode_Functions:
				case kATProfileMode_BasicBlock:
					mListColumnNames.emplace_back(L"Address");
					break;
				case kATProfileMode_BasicLines:
					mListColumnNames.emplace_back(L"Line");
					break;
			}

			mListColumnNames.emplace_back(L"Calls");
			mListColumnNames.emplace_back(L"Clocks");
			mListColumnNames.emplace_back(L"Insns");
			mListColumnNames.emplace_back(L"Clocks%");
			mListColumnNames.emplace_back(L"Insns%");
			mListColumnNames.emplace_back(L"CPUClocks");
			mListColumnNames.emplace_back(L"CPUClocks%");
			mListColumnNames.emplace_back(L"DMA%");

			for (auto cm : mProfileSessionCounterModes) {
				if (cm) {
					mListColumnNames.emplace_back(kCounterModeColumnNames[cm - 1]);
					mListColumnNames.emplace_back(VDStringW(kCounterModeColumnNames[cm - 1]) + L"%");
				}
			}

			int nc = 1;

			for (const auto& name : mListColumnNames)
				mListView.InsertColumn(nc++, name.c_str(), 0, nc > 1);

			mListView.InsertColumn(nc, L"", 0, false);		// crude hack to fix full justified column

			size_t n = mpRecords->size();
			mVLItems.resize(n);

			for(uint32 i=0; i<n; ++i) {
				mVLItems[i].Init((int)i, this);
				mListView.InsertVirtualItem(i, &mVLItems[i]);
			}

			mListView.AutoSizeColumns();
			mListView.Sort(mComparer);
		}
	}

	mTreeView.SetRedraw(true);
	mListView.SetRedraw(true);
}

namespace {
	void MergeRecords(ATProfileFrame& dst, const ATProfileSession& srcSession, uint32 start, uint32 end, ATProfileFrame::Records ATProfileFrame::*field) {
		auto& dstRecords = dst.*field;

		vdhashmap<uint32, uint32> addressLookup;

		for(uint32 i = start; i < end; ++i) {
			const auto& srcFrame = *srcSession.mpFrames[i];
			const auto& srcRecords = srcFrame.*field;

			for(const auto& srcRecord : srcRecords) {
				auto r = addressLookup.insert(srcRecord.mAddress);

				if (r.second) {
					uint32 newIndex = (uint32)dstRecords.size();
					r.first->second = newIndex;

					dstRecords.push_back(srcRecord);
				} else {
					auto& dstRecord = dstRecords[r.first->second];

					dstRecord.mCalls += srcRecord.mCalls;
					dstRecord.mInsns += srcRecord.mInsns;
					dstRecord.mCycles += srcRecord.mCycles;
					dstRecord.mUnhaltedCycles += srcRecord.mUnhaltedCycles;

					for(uint32 j = 0; j < vdcountof(srcRecord.mCounters); ++j)
						dstRecord.mCounters[j] += srcRecord.mCounters[j];
				}
			}
		}
	}

	void MergeCallGraphContextRecords(ATProfileFrame& dst, const ATProfileSession& srcSession, uint32 start, uint32 end) {
		auto& dstRecords = dst.mCallGraphRecords;

		for(uint32 i = start; i < end; ++i) {
			const auto& srcFrame = *srcSession.mpFrames[i];
			const auto& srcRecords = srcFrame.mCallGraphRecords;

			const uint32 srcCount = (uint32)srcRecords.size();
			const uint32 dstCount = (uint32)dstRecords.size();
			const uint32 minCount = std::min(srcCount, dstCount);
			
			dstRecords.resize(std::max(srcCount, dstCount));

			for(uint32 j=0; j<minCount; ++j) {
				auto& dstRecord = dstRecords[j];
				const auto& srcRecord = srcRecords[j];

				dstRecord.mInsns += srcRecord.mInsns;
				dstRecord.mCycles += srcRecord.mCycles;
				dstRecord.mUnhaltedCycles += srcRecord.mUnhaltedCycles;
				dstRecord.mCalls += srcRecord.mCalls;
			}

			if (srcCount > dstCount)
				std::copy(srcRecords.begin() + dstCount, srcRecords.end(), dstRecords.begin() + dstCount);
		}
	}
}

void ATUIProfilerPane::MergeFrames(uint32 start, uint32 end) {
	if (mSession.mpFrames.empty())
		return;

	mpMergedFrame.clear();
	mpCurrentFrame = nullptr;

	uint32 n = (uint32)mSession.mpFrames.size();
	if (start > n - 1)
		start = n - 1;

	if (end < start)
		end = start + 1;

		// if we have a call graph session, we must clone anyway to compute inclusive times
	if (start + 1 == end && mSession.mContexts.empty()) {
		mpCurrentFrame = mSession.mpFrames[start];
	} else {
		mpMergedFrame = new MergedProfileFrame;
		mpCurrentFrame = mpMergedFrame;

		MergeRecords(*mpMergedFrame, mSession, start, end, &ATProfileFrame::mRecords);
		MergeRecords(*mpMergedFrame, mSession, start, end, &ATProfileFrame::mBlockRecords);
		MergeCallGraphContextRecords(*mpMergedFrame, mSession, start, end);

		mpMergedFrame->mTotalCycles = 0;
		mpMergedFrame->mTotalUnhaltedCycles = 0;
		mpMergedFrame->mTotalInsns = 0;

		for(uint32 i = start; i < end; ++i) {
			const auto& frame = *mSession.mpFrames[i];

			mpMergedFrame->mTotalCycles += frame.mTotalCycles;
			mpMergedFrame->mTotalUnhaltedCycles += frame.mTotalUnhaltedCycles;
			mpMergedFrame->mTotalInsns += frame.mTotalInsns;
		}

		const size_t numRecs = mpCurrentFrame->mCallGraphRecords.size();
		mpMergedFrame->mInclusiveRecords.resize(numRecs, {});

		ATProfileComputeInclusiveStats(mpMergedFrame->mInclusiveRecords.data(), mpMergedFrame->mCallGraphRecords.data(), mSession.mContexts.data(), numRecs);
	}

	RemakeView();
}

void ATUIProfilerPane::CopyAsCsv() {
	if (!(mListView.IsVisible()) || mListColumnNames.empty())
		return;

	VDStringW text;

	const auto appendQuoted = [&](const wchar_t *s) {
		if (wcschr(s, ' ') || wcschr(s, ',') || wcschr(s, '"')) {
			text.push_back('"');

			while(const wchar_t c = *s++) {
				if (c == '"')
					text.push_back('"');

				text.push_back(c);
			}

			text.push_back('"');
		} else {
			text.append(s, s + wcslen(s));
		}
		text.push_back(',');
	};

	const auto endLine = [&] {
		text.pop_back();
		text.push_back('\r');
		text.push_back('\n');
	};

	for(const VDStringW& columnName : mListColumnNames) {
		appendQuoted(columnName.c_str());
	}

	endLine();

	const uint32 numColumns = (uint32)mListColumnNames.size();
	VDStringW cellText;
	for(const auto& item : mVLItems) {
		for(uint32 i = 0; i < numColumns; ++i) {
			cellText.clear();
			item.GetText(i, cellText);

			appendQuoted(cellText.c_str());
		}
		endLine();
	}

	ATCopyTextToClipboard(mhwnd, text.c_str());
}

void ATUIProfilerPane::StartProfiler() {
	if (g_sim.IsProfilingEnabled())
		return;

	uint32 param = 0;
	uint32 param2 = 0;

	if (mBoundaryRule == kATProfileBoundaryRule_PCAddressFunction) {
		param = ATGetDebugger()->EvaluateThrow(mBoundaryAddrExpr.c_str());
	} else if (mBoundaryRule == kATProfileBoundaryRule_PCAddress) {
		param = ATGetDebugger()->EvaluateThrow(mBoundaryAddrExpr.c_str());

		param2 = (uint32)0 - 1;
		if (!mBoundaryAddrExpr2.empty()) {
			param2 = ATGetDebugger()->EvaluateThrow(mBoundaryAddrExpr2.c_str());
		}
	}

	mCapturedProfileMode = mProfileMode;

	g_sim.SetProfilingEnabled(true);

	auto *profiler = g_sim.GetProfiler();
	profiler->SetBoundaryRule(mBoundaryRule, param, param2);
	profiler->Start(mProfileMode, mProfileCounterModes[0], mProfileCounterModes[1]);
}

void ATUIProfilerPane::VLGetText(int item, int subItem, VDStringW& s) const {
	const ATProfileRecord& record = (*mpRecords)[item];

	switch(subItem) {
		case 0:
			switch(record.mAddress & 0x7000000) {
				case 0x3000000:
					s = L"VBI";
					break;

				case 0x4000000:
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

				if (mCapturedProfileMode == kATProfileMode_BasicLines) {
					s.sprintf(L"%u", addr);
				} else if (addr >= 0x10000) {
					if (addr == 0xFFFFFF)
						s = L"Unk.";
					else
						s.sprintf(L"%02X:%04X", addr >> 16, addr & 0xffff);
				} else {
					ATSymbol sym;
					if (ATGetDebuggerSymbolLookup()->LookupSymbol(addr, kATSymbol_Execute, sym)) {
						if (sym.mOffset == addr)
							s.sprintf(L"%04X (%hs)", addr, sym.mpName);
						else
							s.sprintf(L"%04X (%hs+%u)", addr, sym.mpName, addr - sym.mOffset);
					} else
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
			s.sprintf(L"%.2f%%", (float)record.mCycles / (float)mpCurrentFrame->mTotalCycles * 100.0f);
			break;

		case 6:
			s.sprintf(L"%.2f%%", (float)record.mInsns / (float)mpCurrentFrame->mTotalInsns * 100.0f);
			break;

		case 7:
			s.sprintf(L"%u", record.mUnhaltedCycles);
			break;

		case 8:
			s.sprintf(L"%.2f%%", (float)record.mUnhaltedCycles / (float)mpCurrentFrame->mTotalUnhaltedCycles * 100.0f);
			break;

		case 9:
			if (record.mCycles)
				s.sprintf(L"%.2f%%", 100.0f * (1.0f - (float)record.mUnhaltedCycles / (float)record.mCycles));
			break;

		case 10:
		case 12:
			if (mProfileSessionCounterModes[subItem > 10])
				s.sprintf(L"%u", record.mCounters[subItem > 10]);
			break;

		case 11:
		case 13:
			if (mProfileSessionCounterModes[subItem > 11])
				s.sprintf(L"%.2f%%", (float)record.mCounters[subItem > 11] / (float)record.mInsns * 100.0f);
			break;

	}
}

void ATUIProfilerPane::VTGetText(int item, VDStringW& s) const {
	const ATProfileCallGraphRecord& cgr = mpCurrentFrame->mCallGraphRecords[item];

	switch(item) {
		case 0:
			s = L"Main";
			break;

		case 1:
			s = L"IRQ";
			break;

		case 2:
			s = L"NMI (VBI)";
			break;

		case 3:
			s = L"NMI (DLI)";
			break;

		default:
			{
				sint32 addr = mSession.mContexts[item].mAddress & 0xffffff;

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

	const float cyclesToPercent = mpCurrentFrame->mTotalCycles ? 100.0f / (float)mpCurrentFrame->mTotalCycles : 0;
	const float unhaltedCyclesToPercent = mpCurrentFrame->mTotalUnhaltedCycles ? 100.0f / (float)mpCurrentFrame->mTotalUnhaltedCycles : 0;
	const float insnsToPercent = mpCurrentFrame->mTotalInsns ? 100.0f / (float)mpCurrentFrame->mTotalInsns : 0;

	const ATProfileCallGraphInclusiveRecord& cgir = mpMergedFrame->mInclusiveRecords[item];
	s.append_sprintf(L": %u cycles (%.2f%%), %u CPU cycles (%.2f%%), %u insns (%.2f%%)"
		, cgir.mInclusiveCycles
		, (float)cgir.mInclusiveCycles * cyclesToPercent
		, cgir.mInclusiveUnhaltedCycles
		, (float)cgir.mInclusiveUnhaltedCycles * unhaltedCyclesToPercent
		, cgir.mInclusiveInsns
		, (float)cgir.mInclusiveInsns * insnsToPercent
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
