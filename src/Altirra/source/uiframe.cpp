//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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
#include <windowsx.h>
#include <hash_map>
#include <vd2/system/vdstl.h>
#include <vd2/system/vectors.h>
#include <vd2/system/w32assist.h>
#include <vd2/system/math.h>
#include "ui.h"
#include "uiframe.h"

///////////////////////////////////////////////////////////////////////////////

extern ATContainerWindow *g_pMainWindow;

///////////////////////////////////////////////////////////////////////////////

namespace ATUIFrame {
	int g_splitterDistH;
	int g_splitterDistV;
}

using namespace ATUIFrame;

void ATInitUIFrameSystem() {
	g_splitterDistH = GetSystemMetrics(SM_CXEDGE) * 2;
	g_splitterDistV = GetSystemMetrics(SM_CYEDGE) * 2;
}

void ATShutdownUIFrameSystem() {
}

///////////////////////////////////////////////////////////////////////////////

ATContainerSplitterBar::ATContainerSplitterBar()
	: mpControlledPane(NULL)
	, mbVertical(false)
	, mDistanceOffset(0)
{
}

bool ATContainerSplitterBar::Init(HWND hwndParent, ATContainerDockingPane *pane, bool vertical) {
	mbVertical = vertical;
	mpControlledPane = pane;

	if (!mhwnd) {
		if (!CreateWindow(MAKEINTATOM(sWndClass), "", WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS, 0, 0, 0, 0, hwndParent, (UINT)0, VDGetLocalModuleHandleW32(), static_cast<VDShaderEditorBaseWindow *>(this)))
			return false;
	}

	return true;
}

void ATContainerSplitterBar::Shutdown() {
	if (mhwnd)
		DestroyWindow(mhwnd);
}

LRESULT ATContainerSplitterBar::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
	case WM_SIZE:
		OnSize();
		break;

	case WM_PAINT:
		OnPaint();
		break;

	case WM_LBUTTONDOWN:
		OnLButtonDown(wParam, (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam));
		return 0;

	case WM_LBUTTONUP:
		OnLButtonUp(wParam, (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam));
		return 0;

	case WM_MOUSEMOVE:
		OnMouseMove(wParam, (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam));
		return 0;

	case WM_CAPTURECHANGED:
		OnCaptureChanged((HWND)lParam);
		return 0;

	case WM_SETCURSOR:
		SetCursor(LoadCursor(NULL, mbVertical ? IDC_SIZEWE : IDC_SIZENS));
		return TRUE;
	}

	return DefWindowProc(mhwnd, msg, wParam, lParam);
}

void ATContainerSplitterBar::OnPaint() {
	PAINTSTRUCT ps;

	if (HDC hdc = BeginPaint(mhwnd, &ps)) {
		RECT r;
		GetClientRect(mhwnd, &r);
		DrawEdge(hdc, &r, EDGE_RAISED, mbVertical ? BF_LEFT|BF_RIGHT|BF_ADJUST : BF_TOP|BF_BOTTOM|BF_ADJUST);
		FillRect(hdc, &r, (HBRUSH)(COLOR_3DFACE+1));

		EndPaint(mhwnd, &ps);
	}
}

void ATContainerSplitterBar::OnSize() {
	InvalidateRect(mhwnd, NULL, TRUE);
}

void ATContainerSplitterBar::OnLButtonDown(WPARAM wParam, int x, int y) {
	POINT pt = {x, y};
	MapWindowPoints(mhwnd, GetParent(mhwnd), &pt, 1);

	RECT r;
	if (!GetClientRect(mhwnd, &r))
		return;

	const vdrect32& rPane = mpControlledPane->GetArea();

	switch(mpControlledPane->GetDockCode()) {
	case kATContainerDockLeft:
		mDistanceOffset = rPane.width() - pt.x;
		break;
	case kATContainerDockRight:
		mDistanceOffset = rPane.width() + pt.x;
		break;
	case kATContainerDockTop:
		mDistanceOffset = rPane.height() - pt.y;
		break;
	case kATContainerDockBottom:
		mDistanceOffset = rPane.height() + pt.y;
		break;
	}

	SetCapture(mhwnd);
}

void ATContainerSplitterBar::OnLButtonUp(WPARAM wParam, int x, int y) {
	if (GetCapture() == mhwnd)
		ReleaseCapture();
}

void ATContainerSplitterBar::OnMouseMove(WPARAM wParam, int x, int y) {
	if (GetCapture() != mhwnd)
		return;

	POINT pt = {x, y};
	MapWindowPoints(mhwnd, GetParent(mhwnd), &pt, 1);

	const vdrect32& rParentPane = mpControlledPane->GetParentPane()->GetArea();
	int parentW = rParentPane.width();
	int parentH = rParentPane.height();

	switch(mpControlledPane->GetDockCode()) {
	case kATContainerDockLeft:
		mpControlledPane->SetDockFraction((float)(mDistanceOffset + pt.x) / (float)parentW);
		break;
	case kATContainerDockRight:
		mpControlledPane->SetDockFraction((float)(mDistanceOffset - pt.x) / (float)parentW);
		break;
	case kATContainerDockTop:
		mpControlledPane->SetDockFraction((float)(mDistanceOffset + pt.y) / (float)parentH);
		break;
	case kATContainerDockBottom:
		mpControlledPane->SetDockFraction((float)(mDistanceOffset - pt.y) / (float)parentH);
		break;
	}
}

void ATContainerSplitterBar::OnCaptureChanged(HWND hwndNewCapture) {
}

///////////////////////////////////////////////////////////////////////////////

ATDragHandleWindow::ATDragHandleWindow()
	: mX(0)
	, mY(0)
{
}

ATDragHandleWindow::~ATDragHandleWindow() {
}

VDGUIHandle ATDragHandleWindow::Create(int x, int y, int cx, int cy, VDGUIHandle parent, int id) {
	HWND hwnd = CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE, (LPCSTR)sWndClass, "", WS_POPUP, x, y, cx, cy, (HWND)parent, (HMENU)id, VDGetLocalModuleHandleW32(), static_cast<VDShaderEditorBaseWindow *>(this));

	if (hwnd)
		ShowWindow(hwnd, SW_SHOWNOACTIVATE);

	return (VDGUIHandle)hwnd;
}

void ATDragHandleWindow::Destroy() {
	if (mhwnd)
		DestroyWindow(mhwnd);
}

int ATDragHandleWindow::HitTest(int screenX, int screenY) {
	int xdist = screenX - mX;
	int ydist = screenY - mY;
	int dist = abs(xdist) + abs(ydist);

	if (dist >= 37)
		return -1;

	if (xdist < -18)
		return kATContainerDockLeft;

	if (xdist > +18)
		return kATContainerDockRight;

	if (ydist < -18)
		return kATContainerDockTop;

	if (ydist > +18)
		return kATContainerDockBottom;

	return kATContainerDockCenter;
}

LRESULT ATDragHandleWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			OnCreate();
			break;

		case WM_MOVE:
			OnMove();
			break;

		case WM_PAINT:
			OnPaint();
			return 0;

		case WM_ERASEBKGND:
			return FALSE;
	}

	return VDShaderEditorBaseWindow::WndProc(msg, wParam, lParam);
}

void ATDragHandleWindow::OnCreate() {
	POINT pt[8]={
		{  0<<0, 37<<0 },
		{  0<<0, 38<<0 },
		{ 37<<0, 75<<0 },
		{ 38<<0, 75<<0 },
		{ 75<<0, 38<<0 },
		{ 75<<0, 37<<0 },
		{ 38<<0,  0<<0 },
		{ 37<<0,  0<<0 },
	};

	HRGN rgn = CreatePolygonRgn(pt, 8, ALTERNATE);
	if (rgn) {
		if (!SetWindowRgn(mhwnd, rgn, TRUE))
			DeleteObject(rgn);
	}

	OnMove();
}

void ATDragHandleWindow::OnMove() {
	RECT r;
	GetWindowRect(mhwnd, &r);
	mX = r.left + 37;
	mY = r.top + 37;
}

void ATDragHandleWindow::OnPaint() {
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(mhwnd, &ps);
	if (hdc) {
		RECT r;
		GetClientRect(mhwnd, &r);
		FillRect(hdc, &r, (HBRUSH)(COLOR_3DFACE + 1));

		int saveIndex = SaveDC(hdc);
		if (saveIndex) {
			SelectObject(hdc, GetStockObject(DC_PEN));

			uint32 a0 = GetSysColor(COLOR_3DSHADOW);
			uint32 a2 = GetSysColor(COLOR_3DFACE);
			uint32 a4 = GetSysColor(COLOR_3DHIGHLIGHT);
			uint32 a1 = (a0|a2) - (((a0^a2) & 0xfefefe)>>1);
			uint32 a3 = (a2|a4) - (((a2^a4) & 0xfefefe)>>1);
			uint32 b0 = GetSysColor(COLOR_3DDKSHADOW);

			MoveToEx(hdc, 0, 37, NULL);
			SetDCPenColor(hdc, a4);
			LineTo(hdc, 37, 0);
			SetDCPenColor(hdc, a3);
			LineTo(hdc, 74, 37);
			SetDCPenColor(hdc, b0);
			LineTo(hdc, 37, 74);
			SetDCPenColor(hdc, a1);
			LineTo(hdc, 0, 37);

			MoveToEx(hdc, 1, 37, NULL);
			SetDCPenColor(hdc, a4);
			LineTo(hdc, 37, 1);
			SetDCPenColor(hdc, a3);
			LineTo(hdc, 73, 37);
			SetDCPenColor(hdc, a0);
			LineTo(hdc, 37, 73);
			SetDCPenColor(hdc, a1);
			LineTo(hdc, 1, 37);

			MoveToEx(hdc, 19, 55, NULL);
			SetDCPenColor(hdc, a1);
			LineTo(hdc, 19, 19);
			LineTo(hdc, 55, 19);
			SetDCPenColor(hdc, a3);
			LineTo(hdc, 55, 55);
			LineTo(hdc, 19, 55);

			MoveToEx(hdc, 20, 54, NULL);
			SetDCPenColor(hdc, a3);
			LineTo(hdc, 20, 20);
			LineTo(hdc, 54, 20);
			SetDCPenColor(hdc, a1);
			LineTo(hdc, 54, 54);
			LineTo(hdc, 20, 54);

			RestoreDC(hdc, saveIndex);
		}

		EndPaint(mhwnd, &ps);
	}
}

///////////////////////////////////////////////////////////////////////////////

ATContainerDockingPane::ATContainerDockingPane(ATContainerWindow *parent)
	: mpParent(parent)
	, mpDockParent(NULL)
	, mDockCode(-1)
	, mDockFraction(0)
	, mCenterCount(0)
	, mbFullScreen(false)
	, mbFullScreenLayout(false)
	, mbPinned(false)
{
}

ATContainerDockingPane::~ATContainerDockingPane() {
	while(!mChildren.empty()) {
		ATContainerDockingPane *child = mChildren.back();
		mChildren.pop_back();

		VDASSERT(child->mpDockParent == this);
		child->mpDockParent = NULL;
		child->mDockCode = -1;
		child->Release();
	}

	DestroyDragHandles();
	DestroySplitter();
}

void ATContainerDockingPane::SetArea(const vdrect32& area, bool parentContainsFullScreen) {
	bool fullScreenLayout = mbFullScreen || parentContainsFullScreen;

	if (mArea == area && mbFullScreenLayout == fullScreenLayout)
		return;

	mArea = area;
	mbFullScreenLayout = fullScreenLayout;

	Relayout();
}

void ATContainerDockingPane::Relayout() {
	mCenterArea = mArea;

	if (mpSplitter) {
		HWND hwndSplitter = mpSplitter->GetHandleW32();

		::ShowWindow(hwndSplitter, mbFullScreenLayout ? SW_HIDE : SW_SHOWNOACTIVATE);

		switch(mDockCode) {
			case kATContainerDockLeft:
				SetWindowPos(hwndSplitter, NULL, mArea.right, mArea.top, g_splitterDistH, mArea.height(), SWP_NOZORDER|SWP_NOACTIVATE);
				break;

			case kATContainerDockRight:
				SetWindowPos(hwndSplitter, NULL, mArea.left - g_splitterDistH, mArea.top, g_splitterDistH, mArea.height(), SWP_NOZORDER|SWP_NOACTIVATE);
				break;

			case kATContainerDockTop:
				SetWindowPos(hwndSplitter, NULL, mArea.left, mArea.bottom, mArea.width(), g_splitterDistV, SWP_NOZORDER|SWP_NOACTIVATE);
				break;

			case kATContainerDockBottom:
				SetWindowPos(hwndSplitter, NULL, mArea.left, mArea.top - g_splitterDistV, mArea.width(), g_splitterDistV, SWP_NOZORDER|SWP_NOACTIVATE);
				break;
		}
	}

	Children::const_iterator it(mChildren.begin()), itEnd(mChildren.end());
	for(; it != itEnd; ++it) {
		ATContainerDockingPane *pane = *it;

		vdrect32 rPane(mCenterArea);
		if (!mbFullScreenLayout) {
			int padX = 0;
			int padY = 0;

			if (pane->mpContent) {
				HWND hwndContent = pane->mpContent->GetHandleW32();

				if (hwndContent) {
					RECT rPad = {0,0,0,0};
					AdjustWindowRect(&rPad, GetWindowLong(hwndContent, GWL_STYLE), FALSE);

					padX += rPad.right - rPad.left;
					padY += rPad.bottom - rPad.top;
				}
			}

			int w = std::max<int>(VDRoundToInt(mArea.width() * pane->mDockFraction), padX);
			int h = std::max<int>(VDRoundToInt(mArea.height() * pane->mDockFraction), padY); 

			switch(pane->mDockCode) {
				case kATContainerDockLeft:
					rPane.right = rPane.left + w;
					mCenterArea.left = rPane.right + g_splitterDistH;
					break;

				case kATContainerDockRight:
					rPane.left = rPane.right - w;
					mCenterArea.right = rPane.left - g_splitterDistH;
					break;

				case kATContainerDockTop:
					rPane.bottom = rPane.top + h;
					mCenterArea.top = rPane.bottom + g_splitterDistV;
					break;

				case kATContainerDockBottom:
					rPane.top = rPane.bottom - h;
					mCenterArea.bottom = rPane.top - g_splitterDistV;
					break;

				case kATContainerDockCenter:
					break;
			}
		}

		pane->SetArea(rPane, mbFullScreenLayout);
	}

	RepositionContent();
}

int ATContainerDockingPane::GetDockCode() const {
	return mDockCode;
}

float ATContainerDockingPane::GetDockFraction() const {
	return mDockFraction;
}

void ATContainerDockingPane::SetDockFraction(float frac) {
	if (frac < 0.0f)
		frac = 0.0f;

	if (frac > 1.0f)
		frac = 1.0f;

	mDockFraction = frac;

	if (mpDockParent)
		mpDockParent->Relayout();
}

ATContainerDockingPane *ATContainerDockingPane::GetCenterPane() const {
	Children::const_iterator it(mChildren.begin()), itEnd(mChildren.end());
	for(; it!=itEnd; ++it) {
		ATContainerDockingPane *child = *it;

		if (child->GetDockCode() == kATContainerDockCenter)
			return child;
	}

	return NULL;
}

uint32 ATContainerDockingPane::GetChildCount() const {
	return mChildren.size();
}

ATContainerDockingPane *ATContainerDockingPane::GetChildPane(uint32 index) const {
	if (index >= mChildren.size())
		return NULL;

	return mChildren[index];
}

void ATContainerDockingPane::SetContent(ATFrameWindow *frame) {
	VDASSERT(!mpContent);
	mpContent = frame;
	frame->SetPane(this);

	RepositionContent();
}

void ATContainerDockingPane::Dock(ATContainerDockingPane *pane, int code) {
	size_t pos = mCenterCount;

	if (code == kATContainerDockCenter)
		++mCenterCount;
	else {
		switch(code) {
			case kATContainerDockLeft:
			case kATContainerDockRight:
				pane->mDockFraction = 1.0f;
				for(Children::const_iterator it(mChildren.begin()), itEnd(mChildren.end()); it != itEnd; ++it) {
					ATContainerDockingPane *child = *it;

					if (child->mDockCode == kATContainerDockLeft || child->mDockCode == kATContainerDockRight)
						pane->mDockFraction -= child->mDockFraction;
				}

				if (pane->mDockFraction < 0.1f)
					pane->mDockFraction = 0.1f;
				else
					pane->mDockFraction *= 0.5f;
				break;

			case kATContainerDockTop:
			case kATContainerDockBottom:
				pane->mDockFraction = 1.0f;
				for(Children::const_iterator it(mChildren.begin()), itEnd(mChildren.end()); it != itEnd; ++it) {
					ATContainerDockingPane *child = *it;

					if (child->mDockCode == kATContainerDockTop || child->mDockCode == kATContainerDockBottom)
						pane->mDockFraction -= child->mDockFraction;
				}

				if (pane->mDockFraction < 0.1f)
					pane->mDockFraction = 0.1f;
				else
					pane->mDockFraction *= 0.5f;
				break;
		}

		if (!mpDockParent)
			pane->mDockFraction *= 0.5f;
	}

	mChildren.insert(mChildren.end() - pos, pane);
	pane->AddRef();
	pane->mpDockParent = this;
	pane->mDockCode = code;

	Relayout();

	pane->CreateSplitter();
}

bool ATContainerDockingPane::Undock(ATFrameWindow *pane) {
	if (mpContent == pane) {
		pane->SetPane(NULL);
		mpContent = NULL;

		if (mpDockParent) {
			RemoveEmptyNode();
			return true;
		}

		return true;
	}

	Children::const_iterator it(mChildren.begin()), itEnd(mChildren.end());
	for(; it!=itEnd; ++it) {
		ATContainerDockingPane *child = *it;

		if (child->Undock(pane))
			return true;
	}

	return false;
}

void ATContainerDockingPane::UpdateActivationState(ATFrameWindow *frame) {
	if (mpContent) {
		HWND hwndContent = mpContent->GetHandleW32();

		if (hwndContent)
			SendMessage(hwndContent, WM_NCACTIVATE, frame == mpContent, 0);
	}

	Children::const_iterator it(mChildren.begin()), itEnd(mChildren.end());
	for(; it!=itEnd; ++it) {
		ATContainerDockingPane *pane = *it;

		pane->UpdateActivationState(frame);
	}
}

void ATContainerDockingPane::CreateDragHandles() {
	if (!mpDragHandle) {
		mpDragHandle = new ATDragHandleWindow;
		POINT pt = { (mCenterArea.left + mCenterArea.right - 75)/2, (mCenterArea.top + mCenterArea.bottom - 75)/2 };

		HWND hwndParent = mpParent->GetHandleW32();
		ClientToScreen(hwndParent, &pt);

		mpDragHandle->Create(pt.x, pt.y, 75, 75, NULL, 0);

		Children::const_iterator it(mChildren.begin()), itEnd(mChildren.end());
		for(; it!=itEnd; ++it) {
			ATContainerDockingPane *pane = *it;
			pane->CreateDragHandles();
		}
	}
}

void ATContainerDockingPane::DestroyDragHandles() {
	Children::const_iterator it(mChildren.begin()), itEnd(mChildren.end());
	for(; it!=itEnd; ++it) {
		ATContainerDockingPane *pane = *it;
		pane->DestroyDragHandles();
	}

	if (mpDragHandle) {
		mpDragHandle->Destroy();
		mpDragHandle = NULL;
	}
}

void ATContainerDockingPane::CreateSplitter() {
	if (mpSplitter)
		return;

	if (mDockCode == kATContainerDockCenter)
		return;

	mpSplitter = new ATContainerSplitterBar;
	if (!mpSplitter->Init(mpParent->GetHandleW32(), this, mDockCode == kATContainerDockLeft || mDockCode == kATContainerDockRight))
		mpSplitter = NULL;

	HWND hwndSplitter = mpSplitter->GetHandleW32();

	vdrect32 rSplit(mArea);

	switch(mDockCode) {
		case kATContainerDockLeft:
			rSplit.left = rSplit.right;
			rSplit.right += g_splitterDistH;
			break;
		case kATContainerDockRight:
			rSplit.right = rSplit.left;
			rSplit.left -= g_splitterDistH;
			break;
		case kATContainerDockTop:
			rSplit.top = rSplit.bottom;
			rSplit.bottom += g_splitterDistV;
			break;
		case kATContainerDockBottom:
			rSplit.bottom = rSplit.top;
			rSplit.top -= g_splitterDistV;
			break;
	}

	SetWindowPos(hwndSplitter, NULL, rSplit.left, rSplit.top, rSplit.width(), rSplit.height(), SWP_NOZORDER|SWP_NOACTIVATE);
}

void ATContainerDockingPane::DestroySplitter() {
	if (!mpSplitter)
		return;

	mpSplitter->Shutdown();
	mpSplitter = NULL;
}

bool ATContainerDockingPane::HitTestDragHandles(int screenX, int screenY, int& code, ATContainerDockingPane **ppPane) {
	if (mpDragHandle) {
		int localCode = mpDragHandle->HitTest(screenX, screenY);

		if (localCode >= 0) {
			code = localCode;
			*ppPane = this;
			AddRef();
			return true;
		}
	}

	Children::const_iterator it(mChildren.begin()), itEnd(mChildren.end());
	for(; it!=itEnd; ++it) {
		ATContainerDockingPane *pane = *it;
		if (pane && pane->HitTestDragHandles(screenX, screenY, code, ppPane))
			return true;
	}

	return false;
}

void ATContainerDockingPane::UpdateFullScreenState() {
	if (mpContent)
		mbFullScreen = mpContent->IsFullScreen();
	else
		mbFullScreen = false;

	Children::const_iterator it(mChildren.begin()), itEnd(mChildren.end());
	for(; it!=itEnd; ++it) {
		ATContainerDockingPane *pane = *it;
		if (pane && pane->IsFullScreen()) {
			mbFullScreen = true;
			break;
		}
	}

	if (mpDockParent)
		mpDockParent->UpdateFullScreenState();
}

bool ATContainerDockingPane::IsFullScreen() const {
	return mbFullScreen;
}

void ATContainerDockingPane::RepositionContent() {
	if (!mpContent)
		return;

	HWND hwndContent = mpContent->GetHandleW32();

	if (mbFullScreenLayout && !mpContent->IsFullScreen())
		ShowWindow(hwndContent, SW_HIDE);
	else {
		ShowWindow(hwndContent, SW_SHOWNOACTIVATE);
		SetWindowPos(hwndContent, NULL, mCenterArea.left, mCenterArea.top, mCenterArea.width(), mCenterArea.height(), SWP_NOZORDER|SWP_NOACTIVATE);
	}
}

void ATContainerDockingPane::RemoveEmptyNode() {
	ATContainerDockingPane *parent = mpDockParent;
	if (!parent || mpContent || mbPinned)
		return;

	if (!mChildren.empty()) {
		ATContainerDockingPane *child = mChildren.back();

		mpContent = child->mpContent;
		child->mpContent = NULL;

		if (mpContent)
			mpContent->SetPane(this);

		for(Children::const_iterator it(child->mChildren.begin()), itEnd(child->mChildren.end()); it != itEnd; ++it) {
			ATContainerDockingPane *child2 = *it;

			mChildren.push_back(child2);
			child2->mpDockParent = this;
		}

		child->mChildren.clear();

		child->RemoveEmptyNode();
		Relayout();
		return;
	}

	DestroySplitter();

	Children::iterator itDel(std::find(parent->mChildren.begin(), parent->mChildren.end(), this));
	VDASSERT(itDel != parent->mChildren.end());
	parent->mChildren.erase(itDel);

	if (mDockCode == kATContainerDockCenter)
		--parent->mCenterCount;

	mpDockParent = NULL;
	mDockCode = -1;
	mDockFraction = 0;
	Release();

	// NOTE: We're dead at this point!

	parent->Relayout();
	if (parent->mChildren.empty())
		parent->RemoveEmptyNode();
}

///////////////////////////////////////////////////////////////////////////////

ATContainerWindow::ATContainerWindow()
	: mpDockingPane(new ATContainerDockingPane(this))
	, mpDragPaneTarget(NULL)
	, mpActiveFrame(NULL)
	, mpFullScreenFrame(NULL)
	, mbBlockActiveUpdates(false)
{
	if (mpDockingPane) {
		mpDockingPane->AddRef();
		mpDockingPane->SetPinned(true);
	}
}

ATContainerWindow::~ATContainerWindow() {
	if (mpDragPaneTarget) {
		mpDragPaneTarget->Release();
		mpDragPaneTarget = NULL;
	}
	if (mpDockingPane) {
		mpDockingPane->Release();
		mpDockingPane = NULL;
	}
}

void *ATContainerWindow::AsInterface(uint32 id) {
	if (id == ATContainerWindow::kTypeID)
		return static_cast<ATContainerWindow *>(this);

	return VDShaderEditorBaseWindow::AsInterface(id);
}

VDGUIHandle ATContainerWindow::Create(int x, int y, int cx, int cy, VDGUIHandle parent) {
	return (VDGUIHandle)CreateWindowEx(WS_EX_CLIENTEDGE, (LPCSTR)sWndClass, "", WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN, x, y, cx, cy, (HWND)parent, NULL, VDGetLocalModuleHandleW32(), static_cast<VDShaderEditorBaseWindow *>(this));
}

void ATContainerWindow::Destroy() {
	if (mhwnd) {
		DestroyWindow(mhwnd);
		mhwnd = NULL;
	}
}

void ATContainerWindow::Relayout() {
	OnSize();
}

ATContainerWindow *ATContainerWindow::GetContainerWindow(HWND hwnd) {
	if (hwnd) {
		ATOM a = (ATOM)GetClassLong(hwnd, GCW_ATOM);

		if (a == sWndClass) {
			VDShaderEditorBaseWindow *w = (VDShaderEditorBaseWindow *)GetWindowLongPtr(hwnd, 0);
			return vdpoly_cast<ATContainerWindow *>(w);
		}
	}

	return NULL;
}

uint32 ATContainerWindow::GetUndockedPaneCount() const {
	return mUndockedFrames.size();
}

ATFrameWindow *ATContainerWindow::GetUndockedPane(uint32 index) const {
	if (index >= mUndockedFrames.size())
		return NULL;

	return mUndockedFrames[index];
}

bool ATContainerWindow::InitDragHandles() {
	mpDockingPane->CreateDragHandles();
	return true;
}

void ATContainerWindow::ShutdownDragHandles() {
	mpDockingPane->DestroyDragHandles();
}

void ATContainerWindow::UpdateDragHandles(int screenX, int screenY) {
	if (mpDragPaneTarget) {
		mpDragPaneTarget->Release();
		mpDragPaneTarget = NULL;
		mDragPaneTargetCode = -1;
	}

	mpDockingPane->HitTestDragHandles(screenX, screenY, mDragPaneTargetCode, &mpDragPaneTarget);
}

ATContainerDockingPane *ATContainerWindow::DockFrame(ATFrameWindow *frame, int code) {
	return DockFrame(frame, mpDockingPane, code);
}

ATContainerDockingPane *ATContainerWindow::DockFrame(ATFrameWindow *frame, ATContainerDockingPane *parent, int code) {
	parent->AddRef();
	if (mpDragPaneTarget)
		mpDragPaneTarget->Release();
	mpDragPaneTarget = parent;
	mDragPaneTargetCode = code;

	return DockFrame(frame);
}

ATContainerDockingPane *ATContainerWindow::DockFrame(ATFrameWindow *frame) {
	if (!mpDragPaneTarget)
		return NULL;

	if (frame) {
		UndockedFrames::iterator it = std::find(mUndockedFrames.begin(), mUndockedFrames.end(), frame);
		if (it != mUndockedFrames.end())
			mUndockedFrames.erase(it);

		HWND hwndFrame = frame->GetHandleW32();

		if (hwndFrame) {
			HWND hwndActive = ::GetFocus();
			if (hwndActive && ::GetWindow(hwndActive, GW_OWNER) != hwndFrame)
				hwndActive = NULL;

			if (::GetForegroundWindow() == hwndFrame)
				::SetForegroundWindow(mhwnd);

			UINT style = GetWindowLong(hwndFrame, GWL_STYLE);
			style |= WS_CHILD | WS_SYSMENU;
			style &= ~(WS_POPUP | WS_THICKFRAME);		// must remove WS_SYSMENU for top level menus to work
			SetWindowLong(hwndFrame, GWL_STYLE, style);

			GUITHREADINFO gti = {sizeof(GUITHREADINFO)};
			::GetGUIThreadInfo(GetCurrentThreadId(), &gti);

			// Prevent WM_CHILDACTIVATE from changing the active window.
			mbBlockActiveUpdates = true;
			SetParent(hwndFrame, mhwnd);
			mbBlockActiveUpdates = false;

			SetWindowPos(hwndFrame, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOACTIVATE);

			UINT exstyle = GetWindowLong(hwndFrame, GWL_EXSTYLE);
			exstyle |= WS_EX_TOOLWINDOW;
			exstyle &= ~WS_EX_WINDOWEDGE;
			SetWindowLong(hwndFrame, GWL_EXSTYLE, exstyle);

			SetWindowPos(hwndFrame, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOACTIVATE);
			SendMessage(mhwnd, WM_CHANGEUISTATE, MAKELONG(UIS_INITIALIZE, UISF_HIDEACCEL|UISF_HIDEFOCUS), 0);

			if (hwndActive)
				::SetFocus(hwndActive);
		}
	}

	vdrefptr<ATContainerDockingPane> newPane(new ATContainerDockingPane(this));

	if (frame) {
		frame->SetContainer(this);
		newPane->SetContent(frame);
	}

	mpDragPaneTarget->Dock(newPane, mDragPaneTargetCode);

	if (frame)
		NotifyFrameActivated(mpActiveFrame);

	return newPane;
}

void ATContainerWindow::AddUndockedFrame(ATFrameWindow *frame) {
	VDASSERT(std::find(mUndockedFrames.begin(), mUndockedFrames.end(), frame) == mUndockedFrames.end());

	mUndockedFrames.push_back(frame);
}

void ATContainerWindow::UndockFrame(ATFrameWindow *frame, bool visible) {
	HWND hwndFrame = frame->GetHandleW32();
	UINT style = GetWindowLong(hwndFrame, GWL_STYLE);

	if (mpActiveFrame == frame) {
		mpActiveFrame = NULL;

		if (!visible)
			::SetFocus(mhwnd);
	}

	if (mpFullScreenFrame == frame) {
		mpFullScreenFrame = frame;
		frame->SetFullScreen(false);
	}

	if (style & WS_CHILD) {
		ShowWindow(hwndFrame, SW_HIDE);
		mpDockingPane->Undock(frame);

		RECT r;
		GetWindowRect(hwndFrame, &r);

		HWND hwndOwner = GetWindow(mhwnd, GW_OWNER);
		SetParent(hwndFrame, hwndOwner);

		style &= ~WS_CHILD;
		style |= WS_OVERLAPPEDWINDOW;
		SetWindowLong(hwndFrame, GWL_STYLE, style);

		UINT exstyle = GetWindowLong(hwndFrame, GWL_EXSTYLE);
		exstyle &= ~WS_EX_TOOLWINDOW;
		SetWindowLong(hwndFrame, GWL_EXSTYLE, exstyle);

		SetWindowPos(hwndFrame, NULL, r.left, r.top, 0, 0, SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOZORDER|SWP_HIDEWINDOW);
		SendMessage(hwndFrame, WM_CHANGEUISTATE, MAKELONG(UIS_INITIALIZE, UISF_HIDEACCEL|UISF_HIDEFOCUS), 0);

		if (visible)
			ShowWindow(hwndFrame, SW_SHOWNA);

		VDASSERT(std::find(mUndockedFrames.begin(), mUndockedFrames.end(), frame) == mUndockedFrames.end());
		mUndockedFrames.push_back(frame);
	}
}

void ATContainerWindow::SetFullScreenFrame(ATFrameWindow *frame) {
	if (mpFullScreenFrame == frame)
		return;

	mpFullScreenFrame = frame;

	LONG exStyle = GetWindowLong(mhwnd, GWL_EXSTYLE);

	if (frame)
		exStyle &= ~WS_EX_CLIENTEDGE;
	else
		exStyle |= WS_EX_CLIENTEDGE;

	SetWindowLong(mhwnd, GWL_EXSTYLE, exStyle);
	SetWindowPos(mhwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

	mpDockingPane->Relayout();
}

void ATContainerWindow::ActivateFrame(ATFrameWindow *frame) {
	if (mpActiveFrame == frame)
		return;

	NotifyFrameActivated(frame);
}

void ATContainerWindow::NotifyFrameActivated(ATFrameWindow *frame) {
	if (mbBlockActiveUpdates)
		return;

	HWND hwndFrame = NULL;
	
	if (frame)
		hwndFrame = frame->GetHandleW32();

	VDASSERT(!hwndFrame || GetAncestor(hwndFrame, GA_ROOT) == mhwnd);
	mpActiveFrame = frame;

	if (mpDockingPane)
		mpDockingPane->UpdateActivationState(frame);
}

void ATContainerWindow::NotifyUndockedFrameDestroyed(ATFrameWindow *frame) {
	UndockedFrames::iterator it = std::find(mUndockedFrames.begin(), mUndockedFrames.end(), frame);
	if (it != mUndockedFrames.end())
		mUndockedFrames.erase(it);
}

LRESULT ATContainerWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			if (!OnCreate())
				return -1;
			break;

		case WM_DESTROY:
			OnDestroy();
			break;

		case WM_SIZE:
			OnSize();
			break;

		case WM_PARENTNOTIFY:
			if (LOWORD(wParam) == WM_CREATE)
				OnSize();
			else if (LOWORD(wParam) == WM_DESTROY)
				OnChildDestroy((HWND)lParam);
			break;

		case WM_NCACTIVATE:
			if (wParam != 0)
				mpDockingPane->UpdateActivationState(mpActiveFrame);
			else
				mpDockingPane->UpdateActivationState(NULL);
			break;

		case WM_SETFOCUS:
			OnSetFocus((HWND)wParam);
			break;

		case WM_KILLFOCUS:
			OnKillFocus((HWND)wParam);
			break;

		case WM_ACTIVATE:
			if (OnActivate(LOWORD(wParam), HIWORD(wParam) != 0, (HWND)lParam))
				return 0;
			break;
	}

	return VDShaderEditorBaseWindow::WndProc(msg, wParam, lParam);
}

bool ATContainerWindow::OnCreate() {
	OnSize();
	return true;
}

void ATContainerWindow::OnDestroy() {
}

void ATContainerWindow::OnSize() {
	RECT r;
	GetClientRect(mhwnd, &r);
	mpDockingPane->SetArea(vdrect32(0, 0, r.right, r.bottom), false);
}

void ATContainerWindow::OnChildDestroy(HWND hwndChild) {
	ATFrameWindow *frame = ATFrameWindow::GetFrameWindow(hwndChild);

	if (frame) {
		if (mpActiveFrame == frame) {
			mpActiveFrame = NULL;

			for(ATContainerDockingPane *pane = frame->GetPane(); pane; pane = pane->GetParentPane()) {
				ATContainerDockingPane *pane2 = pane->GetCenterPane();
				
				if (!pane2)
					continue;

				ATFrameWindow *frame2 = pane2->GetContent();

				if (frame2 && frame2 != frame) {
					::SetFocus(frame2->GetHandleW32());
					NotifyFrameActivated(frame2);
					break;
				}
			}
		}

		UndockFrame(frame);
	}
}

void ATContainerWindow::OnSetFocus(HWND hwndOldFocus) {
	if (mpActiveFrame) {
		VDASSERT(mpActiveFrame->GetContainer() == this);

		NotifyFrameActivated(mpActiveFrame);

		HWND hwndActiveFrame = mpActiveFrame->GetHandleW32();
		SetFocus(hwndActiveFrame);
	}
}

void ATContainerWindow::OnKillFocus(HWND hwndNewFocus) {
}

bool ATContainerWindow::OnActivate(UINT code, bool minimized, HWND hwnd) {
	if (code == WA_INACTIVE) {
#if 0
		HWND hwndFocus = GetFocus();

		if (IsChild(mhwnd, hwndFocus)) {
			HWND hwndFocusTopLevel = hwndFocus;
			while(GetWindowLong(hwndFocusTopLevel, GWL_STYLE) & WS_CHILD) {
				HWND next = GetParent(hwndFocusTopLevel);
				VDASSERT(next);
				hwndFocusTopLevel = next;
			}

			if (hwndFocusTopLevel == mhwnd) {
				VDASSERT(GetAncestor(hwndFocus, GA_ROOT) == mhwnd);
				mhwndActiveFrame = hwndFocus;
			}
		}
#endif
	} else if (!minimized) {
		if (mpActiveFrame) {
			VDASSERT(mpActiveFrame->GetContainer() == this);

			NotifyFrameActivated(mpActiveFrame);

			HWND hwndActiveFrame = mpActiveFrame->GetHandleW32();
			if (hwndActiveFrame)
				SetFocus(hwndActiveFrame);
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////

ATFrameWindow::ATFrameWindow()
	: mbDragging(false)
	, mbFullScreen(false)
	, mpDockingPane(NULL)
	, mpContainer(NULL)
{
}

ATFrameWindow::~ATFrameWindow() {
}

ATFrameWindow *ATFrameWindow::GetFrameWindow(HWND hwnd) {
	if (hwnd) {
		VDShaderEditorBaseWindow *w = (VDShaderEditorBaseWindow *)GetWindowLongPtr(hwnd, 0);
		return vdpoly_cast<ATFrameWindow *>(w);
	}

	return NULL;
}

void *ATFrameWindow::AsInterface(uint32 iid) {
	if (iid == ATFrameWindow::kTypeID)
		return static_cast<ATFrameWindow *>(this);

	return VDShaderEditorBaseWindow::AsInterface(iid);
}

bool ATFrameWindow::IsFullScreen() const {
	return mbFullScreen;
}

void ATFrameWindow::SetFullScreen(bool fs) {
	if (mbFullScreen == fs)
		return;

	mbFullScreen = fs;

	if (mpContainer)
		mpContainer->SetFullScreenFrame(fs ? this : NULL);

	if (mpDockingPane)
		mpDockingPane->UpdateFullScreenState();

	if (mhwnd) {
		LONG style = GetWindowLong(mhwnd, GWL_STYLE);
		LONG exStyle = GetWindowLong(mhwnd, GWL_EXSTYLE);

		if (fs) {
			style &= ~(WS_CAPTION | WS_THICKFRAME);
			style |= WS_POPUP;
			exStyle &= WS_EX_TOOLWINDOW;
		} else {
			style &= ~WS_POPUP;
			style |= WS_CAPTION;

			if (style & WS_CHILD)
				exStyle |= WS_EX_TOOLWINDOW;
			else
				style |= WS_THICKFRAME;
		}

		SetWindowLong(mhwnd, GWL_STYLE, style);
		SetWindowLong(mhwnd, GWL_EXSTYLE, exStyle);
		SetWindowPos(mhwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}
}

VDGUIHandle ATFrameWindow::Create(const char *title, int x, int y, int cx, int cy, VDGUIHandle parent) {
	return (VDGUIHandle)CreateWindowEx(0, (LPCSTR)sWndClass, title, WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN|WS_CLIPSIBLINGS, x, y, cx, cy, (HWND)parent, NULL, VDGetLocalModuleHandleW32(), static_cast<VDShaderEditorBaseWindow *>(this));
}

VDGUIHandle ATFrameWindow::CreateChild(const char *title, int x, int y, int cx, int cy, VDGUIHandle parent) {
	return (VDGUIHandle)CreateWindowEx(WS_EX_TOOLWINDOW, (LPCSTR)sWndClass, title, WS_CHILD|WS_CAPTION|WS_CLIPCHILDREN|WS_CLIPSIBLINGS, x, y, cx, cy, (HWND)parent, NULL, VDGetLocalModuleHandleW32(), static_cast<VDShaderEditorBaseWindow *>(this));
}

LRESULT ATFrameWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			if (!OnCreate())
				return -1;
			break;

		case WM_DESTROY:
			OnDestroy();
			break;

		case WM_SIZE:
			OnSize();
			break;

		case WM_PARENTNOTIFY:
			if (LOWORD(wParam) == WM_CREATE)
				OnSize();
			break;

		case WM_NCLBUTTONDOWN:
			if (OnNCLButtonDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
				return 0;
			break;

		case WM_LBUTTONUP:
			if (mbDragging) {
				EndDrag(true);
			}
			break;

		case WM_MOUSEMOVE:
			if (OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
				return 0;
			break;

		case WM_CAPTURECHANGED:
			if ((HWND)lParam != mhwnd) {
				EndDrag(false);
			}
			break;

		case WM_KEYDOWN:
			if (mbDragging) {
				if (wParam == VK_ESCAPE) {
					EndDrag(false);
				}
			}
			break;

		case WM_MENUCHAR:
			if (GetWindowLong(mhwnd, GWL_STYLE) & WS_CAPTION) {
				if (HWND hwndParent = GetParent(mhwnd)) {
					PostMessage(hwndParent, WM_SYSCOMMAND, SC_KEYMENU, wParam);
					return MAKELRESULT(0, 1);
				}
			}
			break;

		case WM_CHILDACTIVATE:
		case WM_MOUSEACTIVATE:
			if (ATContainerWindow *cont = ATContainerWindow::GetContainerWindow(GetAncestor(mhwnd, GA_ROOTOWNER)))
				cont->NotifyFrameActivated(this);
			break;

		case WM_SETFOCUS:
			{
				HWND hwndChild = GetWindow(mhwnd, GW_CHILD);

				if (hwndChild)
					SetFocus(hwndChild);
			}
			return 0;
	}

	return VDShaderEditorBaseWindow::WndProc(msg, wParam, lParam);
}

bool ATFrameWindow::OnCreate() {
	OnSize();
	return true;
}

void ATFrameWindow::OnDestroy() {
	if (!mpDockingPane && mpContainer)
		mpContainer->NotifyUndockedFrameDestroyed(this);
}

void ATFrameWindow::OnSize() {
	RECT r;
	if (GetClientRect(mhwnd, &r)) {
		HWND hwndChild = GetWindow(mhwnd, GW_CHILD);

		if (hwndChild)
			SetWindowPos(hwndChild, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE);
	}
}

bool ATFrameWindow::OnNCLButtonDown(int code, int x, int y) {
	if (code != HTCAPTION)
		return false;

	RECT r;

	mbDragging = true;
	GetWindowRect(mhwnd, &r);

	mDragOffsetX = r.left - x;
	mDragOffsetY = r.top - y;

	mpDragContainer = ATContainerWindow::GetContainerWindow(GetWindow(mhwnd, GW_OWNER));

	UINT style = GetWindowLong(mhwnd, GWL_STYLE);
	if (style & WS_CHILD) {
		mpDragContainer->UndockFrame(this);
	}

	SetForegroundWindow(mhwnd);
	SetActiveWindow(mhwnd);
	SetFocus(mhwnd);
	SetCapture(mhwnd);
	if (mpDragContainer) {
		mpDragContainer->InitDragHandles();
	}
	return true;
}

bool ATFrameWindow::OnMouseMove(int x, int y) {
	if (!mbDragging)
		return false;

	POINT pt = {x, y};
	ClientToScreen(mhwnd, &pt);
	SetWindowPos(mhwnd, NULL, pt.x + mDragOffsetX, pt.y + mDragOffsetY, 0, 0, SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);

	if (mpDragContainer)
		mpDragContainer->UpdateDragHandles(pt.x, pt.y);

	return true;
}

void ATFrameWindow::EndDrag(bool success) {
	if (mbDragging) {
		mbDragging = false;		// also prevents recursion
		if (GetCapture() == mhwnd)
			ReleaseCapture();

		if (mpDragContainer) {
			if (success) {
				mpDragContainer->DockFrame(this);
			}

			mpDragContainer->ShutdownDragHandles();
			mpDragContainer = NULL;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

namespace {
	typedef stdext::hash_map<uint32, ATPaneCreator> PaneCreators;
	PaneCreators g_paneCreatorMap;

	typedef stdext::hash_map<uint32, ATUIPane *> ActivePanes;
	ActivePanes g_activePanes;

	HFONT	g_monoFont;
}

void ATRegisterUIPaneType(uint32 id, ATPaneCreator creator) {
	g_paneCreatorMap[id] = creator;
}

void ATRegisterActiveUIPane(uint32 id, ATUIPane *w) {
	g_activePanes[id] = w;
}

void ATUnregisterActiveUIPane(uint32 id, ATUIPane *w) {
	g_activePanes.erase(id);
}

ATUIPane *ATGetUIPane(uint32 id) {
	ActivePanes::const_iterator it(g_activePanes.find(id));

	return it != g_activePanes.end() ? it->second : NULL;
}

ATUIPane *ATGetUIPaneByFrame(ATFrameWindow *frame) {
	if (!frame)
		return NULL;

	HWND hwndParent = frame->GetHandleW32();

	ActivePanes::const_iterator it(g_activePanes.begin()), itEnd(g_activePanes.end());
	for(; it != itEnd; ++it) {
		ATUIPane *pane = it->second;
		HWND hwndPane = pane->GetHandleW32();

		if (!hwndPane)
			continue;

		if (GetParent(hwndPane) == hwndParent)
			return pane;
	}

	return NULL;
}

void ATActivateUIPane(uint32 id, bool giveFocus, bool visible) {
	vdrefptr<ATUIPane> pane(ATGetUIPane(id));

	if (!pane) {
		PaneCreators::const_iterator it(g_paneCreatorMap.find(id));
		if (it == g_paneCreatorMap.end())
			return;
	
		if (!it->second(~pane))
			return;

		vdrefptr<ATFrameWindow> frame(new ATFrameWindow);
		frame->Create(pane->GetUIPaneName(), CW_USEDEFAULT, CW_USEDEFAULT, 300, 200, (VDGUIHandle)g_pMainWindow->GetHandleW32());
		pane->Create(frame);

		int preferredCode = pane->GetPreferredDockCode();
		if (preferredCode >= 0 && visible)
			g_pMainWindow->DockFrame(frame, preferredCode);
		else
			g_pMainWindow->AddUndockedFrame(frame);

		if (visible)
			ShowWindow(frame->GetHandleW32(), SW_SHOWNOACTIVATE);
	}

	if (giveFocus) {
		HWND hwndPane = pane->GetHandleW32();
		HWND hwndPaneParent = GetParent(hwndPane);
		SetFocus(hwndPane);

		if (hwndPaneParent) {
			ATFrameWindow *frame = ATFrameWindow::GetFrameWindow(hwndPaneParent);
			if (frame)
				g_pMainWindow->NotifyFrameActivated(frame);
		}
	}
}

ATUIPane::ATUIPane(uint32 paneId, const char *name)
	: mPaneId(paneId)
	, mpName(name)
	, mDefaultWindowStyles(WS_CHILD|WS_CLIPCHILDREN)
	, mPreferredDockCode(-1)
{
}

ATUIPane::~ATUIPane() {
}

bool ATUIPane::Create(ATFrameWindow *frame) {
	HWND hwnd = CreateWindow((LPCSTR)sWndClass, "", mDefaultWindowStyles & ~WS_VISIBLE, 0, 0, 0, 0, frame->GetHandleW32(), (HMENU)100, VDGetLocalModuleHandleW32(), static_cast<VDShaderEditorBaseWindow *>(this));

	if (!hwnd)
		return false;

	::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
	return true;
}

LRESULT ATUIPane::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			if (!OnCreate())
				return -1;
			break;

		case WM_DESTROY:
			OnDestroy();
			break;

		case WM_SIZE:
			OnSize();
			break;

		case WM_SETFOCUS:
			OnSetFocus();
			return 0;
	}

	return VDShaderEditorBaseWindow::WndProc(msg, wParam, lParam);
}

bool ATUIPane::OnCreate() {
	RegisterUIPane();
	OnSize();
	return true;
}

void ATUIPane::OnDestroy() {
	UnregisterUIPane();
}

void ATUIPane::OnSize() {
}

void ATUIPane::OnSetFocus() {
}

void ATUIPane::RegisterUIPane() {
	ATRegisterActiveUIPane(mPaneId, this);
}

void ATUIPane::UnregisterUIPane() {
	ATUnregisterActiveUIPane(mPaneId, this);
}
