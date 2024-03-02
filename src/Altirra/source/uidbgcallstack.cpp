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

#include "stdafx.h"
#include <vd2/system/w32assist.h>
#include "uidbgcallstack.h"

ATCallStackWindow::ATCallStackWindow()
	: ATUIDebuggerPaneWindow(kATUIPaneId_CallStack, L"Call Stack")
	, mhwndList(NULL)
{
	mPreferredDockCode = kATContainerDockRight;
}

ATCallStackWindow::~ATCallStackWindow() {
}

LRESULT ATCallStackWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_SIZE:
			OnSize();
			break;

		case WM_COMMAND:
			if (LOWORD(wParam) == 100 && HIWORD(wParam) == LBN_DBLCLK) {
				int idx = (int)SendMessage(mhwndList, LB_GETCURSEL, 0, 0);

				if ((size_t)idx < mFrames.size())
					ATGetDebugger()->SetFramePC(mFrames[idx]);
			}
			break;

		case WM_VKEYTOITEM:
			if (LOWORD(wParam) == VK_ESCAPE) {
				ATUIPane *pane = ATGetUIPane(kATUIPaneId_Console);

				if (pane)
					ATActivateUIPane(kATUIPaneId_Console, true);
				return -2;
			}
			break;
	}

	return ATUIDebuggerPaneWindow::WndProc(msg, wParam, lParam);
}

bool ATCallStackWindow::OnCreate() {
	if (!ATUIDebuggerPaneWindow::OnCreate())
		return false;

	mhwndList = CreateWindowEx(0, _T("LISTBOX"), _T(""), WS_CHILD|WS_VISIBLE|LBS_HASSTRINGS|LBS_NOTIFY|LBS_WANTKEYBOARDINPUT, 0, 0, 0, 0, mhwnd, (HMENU)100, VDGetLocalModuleHandleW32(), NULL);
	if (!mhwndList)
		return false;

	OnFontsUpdated();

	OnSize();

	ATGetDebugger()->AddClient(this, true);
	return true;
}

void ATCallStackWindow::OnDestroy() {
	ATGetDebugger()->RemoveClient(this);
	ATUIDebuggerPaneWindow::OnDestroy();
}

void ATCallStackWindow::OnSize() {
	RECT r;
	if (mhwndList && GetClientRect(mhwnd, &r)) {
		VDVERIFY(SetWindowPos(mhwndList, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE));
	}
}

void ATCallStackWindow::OnFontsUpdated() {
	if (mhwndList)
		SendMessage(mhwndList, WM_SETFONT, (WPARAM)ATGetConsoleFontW32(), TRUE);
}

void ATCallStackWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	IATDebugger *db = ATGetDebugger();
	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();
	ATCallStackFrame frames[16];
	uint32 n = db->GetCallStack(frames, 16);

	mFrames.resize(n);

	int selIdx = (int)SendMessage(mhwndList, LB_GETCURSEL, 0, 0);

	SendMessage(mhwndList, LB_RESETCONTENT, 0, 0);
	for(uint32 i=0; i<n; ++i) {
		const ATCallStackFrame& fr = frames[i];

		mFrames[i] = fr.mPC;

		ATSymbol sym;
		const char *symname = "";
		if (dbs->LookupSymbol(fr.mPC, kATSymbol_Execute, sym))
			symname = sym.mpName;

		mState.sprintf(L"%c%04X: %c%04X (%hs)"
			, (state.mFrameExtPC ^ fr.mPC) & 0xFFFF ? L' ' : L'>'		// not entirely correct, but we don't have full info
			, fr.mSP
			, fr.mP & 0x04 ? L'*' : L' '
			, fr.mPC
			, symname);
		SendMessageW(mhwndList, LB_ADDSTRING, 0, (LPARAM)mState.c_str());
	}

	if (selIdx >= 0 && (uint32)selIdx < n)
		SendMessageW(mhwndList, LB_SETCURSEL, selIdx, 0);
}

void ATUIDebuggerRegisterCallStackPane() {
	ATRegisterUIPaneType(kATUIPaneId_CallStack, VDRefCountObjectFactory<ATCallStackWindow, ATUIPane>);
}
