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
#include "ui.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;
ATOM VDShaderEditorBaseWindow::sWndClass;

VDShaderEditorBaseWindow::VDShaderEditorBaseWindow()
	: mRefCount(0)
	, mhwnd(NULL)
{
}

VDShaderEditorBaseWindow::~VDShaderEditorBaseWindow() {
}

ATOM VDShaderEditorBaseWindow::Register() {
	if (sWndClass)
		return sWndClass;

	WNDCLASS wc;

	wc.style			= CS_DBLCLKS;
	wc.lpfnWndProc		= StaticWndProc;
	wc.cbClsExtra		= 0;
	wc.cbWndExtra		= sizeof(VDShaderEditorBaseWindow *);
	wc.hInstance		= (HINSTANCE)&__ImageBase;
	wc.hIcon			= NULL;
	wc.hCursor			= LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground	= (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName		= NULL;
	wc.lpszClassName	= "VDShaderEditorBaseWindow";

	sWndClass = RegisterClass(&wc);
	return sWndClass;
}

void VDShaderEditorBaseWindow::Unregister() {
	if (sWndClass) {
		UnregisterClass((LPCTSTR)sWndClass, (HINSTANCE)&__ImageBase);
		sWndClass = NULL;
	}
}

int VDShaderEditorBaseWindow::AddRef() {
	return ++mRefCount;
}

int VDShaderEditorBaseWindow::Release() {
	int rc = --mRefCount;

	if (!rc)
		delete this;

	return 0;
}

void *VDShaderEditorBaseWindow::AsInterface(uint32 iid) {
	return NULL;
}

LRESULT VDShaderEditorBaseWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	VDShaderEditorBaseWindow *p;

	if (msg == WM_NCCREATE) {
		p = (VDShaderEditorBaseWindow *)((LPCREATESTRUCT)lParam)->lpCreateParams;
		SetWindowLongPtr(hwnd, 0, (LONG_PTR)p);
		p->AddRef();
		p->mhwnd = hwnd;
	} else
		p = (VDShaderEditorBaseWindow *)GetWindowLongPtr(hwnd, 0);

	if (!p)
		return DefWindowProc(hwnd, msg, wParam, lParam);

	p->AddRef();
	LRESULT result = p->WndProc(msg, wParam, lParam);

	if (msg == WM_NCDESTROY && p) {
		p->mhwnd = NULL;
		p->Release();
		SetWindowLongPtr(hwnd, 0, NULL);
	}
	p->Release();

	return result;
}

LRESULT VDShaderEditorBaseWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	return DefWindowProc(mhwnd, msg, wParam, lParam);
}

///////////////////////////////////////////////////////////////////////////////

VDShaderEditorSplitterBar::VDShaderEditorSplitterBar()
	: mFraction(0.5f)
	, mDragOffset(0)
{
	mSplitter.left = 0;
	mSplitter.top = 0;
	mSplitter.right = 0;
	mSplitter.bottom = 0;
}

LRESULT VDShaderEditorSplitterBar::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
	case WM_CREATE:
		ConvertFractionToLocation();
		break;

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
		if ((HWND)wParam == mhwnd && LOWORD(lParam) == HTCLIENT) {
			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(mhwnd, &pt);
			SetCursor(LoadCursor(NULL, PtInRect(&mSplitter, pt) ? IDC_SIZENS : IDC_ARROW));
			return TRUE;
		}
		break;

	case WM_COMMAND:
		if (HWND hwndParent = GetParent(mhwnd))
			return SendMessage(hwndParent, msg, wParam, lParam);
		break;
	}

	return DefWindowProc(mhwnd, msg, wParam, lParam);
}

void VDShaderEditorSplitterBar::OnPaint() {
	PAINTSTRUCT ps;

	if (HDC hdc = BeginPaint(mhwnd, &ps)) {
		RECT r(mSplitter);
		DrawEdge(hdc, &r, EDGE_RAISED, BF_TOP|BF_BOTTOM|BF_ADJUST);
		FillRect(hdc, &r, (HBRUSH)(COLOR_3DFACE+1));

		EndPaint(mhwnd, &ps);
	}
}

void VDShaderEditorSplitterBar::OnSize() {
	ConvertFractionToLocation();
	InvalidateRect(mhwnd, NULL, TRUE);

	RECT rClient;
	GetClientRect(mhwnd, &rClient);

	HWND hwnd1 = GetWindow(mhwnd, GW_CHILD);
	HWND hwnd2 = NULL;

	if (hwnd1)
		hwnd2 = GetWindow(hwnd1, GW_HWNDNEXT);

	if (hwnd1)
		SetWindowPos(hwnd1, NULL, 0, 0, rClient.right, mSplitter.top, SWP_NOZORDER | SWP_NOACTIVATE);

	if (hwnd2) {
		int h2 = rClient.bottom - mSplitter.bottom;
		if (h2 < 0)
			h2 = 0;
		SetWindowPos(hwnd2, NULL, 0, mSplitter.bottom, rClient.right, h2, SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

void VDShaderEditorSplitterBar::OnLButtonDown(WPARAM wParam, int x, int y) {
	POINT pt={x,y};

	if (PtInRect(&mSplitter, pt)) {
		mDragOffset = mSplitter.top - y;
		SetCapture(mhwnd);
		LockWindowUpdate(mhwnd);
		DrawMovingSplitter();		
	}
}

void VDShaderEditorSplitterBar::OnLButtonUp(WPARAM wParam, int x, int y) {
	if (GetCapture() == mhwnd)
		ReleaseCapture();
}

void VDShaderEditorSplitterBar::OnMouseMove(WPARAM wParam, int x, int y) {
	if (GetCapture() == mhwnd) {
		RECT rClient;

		DrawMovingSplitter();
		GetClientRect(mhwnd, &rClient);
		int ymax = rClient.bottom - GetSystemMetrics(SM_CYEDGE)*3;
		int ysplit = mDragOffset + y;
		if (ysplit > ymax)
			ysplit = ymax;
		if (ysplit < 0)
			ysplit = 0;
		OffsetRect(&mSplitter, 0, ysplit - mSplitter.top);
		DrawMovingSplitter();
	}
}

void VDShaderEditorSplitterBar::OnCaptureChanged(HWND hwndNewCapture) {
	DrawMovingSplitter();
	LockWindowUpdate(NULL);
	ConvertLocationToFraction();
	OnSize();
}

void VDShaderEditorSplitterBar::DrawMovingSplitter() {
	if (HDC hdc = GetDCEx(mhwnd, NULL, DCX_LOCKWINDOWUPDATE|DCX_CACHE)) {
		InvertRect(hdc, &mSplitter);
		ReleaseDC(mhwnd, hdc);
	}
}

void VDShaderEditorSplitterBar::ConvertLocationToFraction() {
	RECT rClient;
	GetClientRect(mhwnd, &rClient);

	const int h = rClient.bottom;
	int sh = GetSystemMetrics(SM_CYEDGE) * 3;

	if (sh > h)
		sh = h;

	if (sh >= h)
		mFraction = 0;
	else
		mFraction = (float)mSplitter.top / (float)(h-sh);

	if (mFraction < 0)
		mFraction = 0;
	else if (mFraction > 1)
		mFraction = 1;
}

void VDShaderEditorSplitterBar::ConvertFractionToLocation() {
	RECT rClient;
	GetClientRect(mhwnd, &rClient);

	const int h = rClient.bottom;
	int sh = GetSystemMetrics(SM_CYEDGE) * 3;

	int y = (int)(0.5 + (h-sh)*mFraction);
	if (y < 0)
		y = 0;

	mSplitter.top = y;
	mSplitter.bottom = y + sh;
	mSplitter.left = rClient.left;
	mSplitter.right = rClient.right;
}
