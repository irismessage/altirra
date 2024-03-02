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

#ifndef UI_H
#define UI_H

#include <windows.h>
#include <vd2/system/unknown.h>

class VDShaderEditorBaseWindow : public IVDUnknown {
public:
	VDShaderEditorBaseWindow();
	virtual ~VDShaderEditorBaseWindow();

	static ATOM Register();
	static void Unregister();

	int AddRef();
	int Release();
	void *AsInterface(uint32 iid);

	HWND GetHandleW32() const { return mhwnd; }

	static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	virtual LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) = 0;

protected:
	int mRefCount;
	HWND mhwnd;

	static ATOM sWndClass;
};

class VDShaderEditorSplitterBar : public VDShaderEditorBaseWindow {
public:
	VDShaderEditorSplitterBar();
	~VDShaderEditorSplitterBar();

protected:
	static LRESULT StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	void OnPaint();
	void OnSize();
	void OnLButtonDown(WPARAM wParam, int x, int y);
	void OnLButtonUp(WPARAM wParam, int x, int y);
	void OnMouseMove(WPARAM wParam, int x, int y);
	void OnCaptureChanged(HWND hwndNewCapture);

	void DrawMovingSplitter();
	void ConvertLocationToFraction();
	void ConvertFractionToLocation();

	float	mFraction;
	RECT	mSplitter;
	int		mDragOffset;
};

#endif
